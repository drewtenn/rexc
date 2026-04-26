// Semantic analysis for the parsed Rexc AST.
//
// The parser proves only that source text has the shape of the grammar. This
// stage proves the program is meaningful enough to lower: functions and locals
// are declared once, names resolve, calls match signatures, expressions have
// compatible primitive types, integer literals fit their target types, and
// break/continue appear only inside loops.
#include "rexc/sema.hpp"
#include "rexc/stdlib.hpp"
#include "rexc/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rexc {
namespace {

PrimitiveType i32_type()
{
	return PrimitiveType{PrimitiveKind::SignedInteger, 32};
}

PrimitiveType bool_type()
{
	return PrimitiveType{PrimitiveKind::Bool};
}

PrimitiveType u8_type()
{
	return PrimitiveType{PrimitiveKind::UnsignedInteger, 8};
}

bool is_comparison_operator(const std::string &op)
{
	return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
	       op == ">=";
}

bool is_logical_operator(const std::string &op)
{
	return op == "&&" || op == "||";
}

std::optional<std::uint64_t> parse_decimal_magnitude(const std::string &literal)
{
	// Parse from the original token text so huge literals are checked here,
	// rather than being silently wrapped by parser-side numeric conversion.
	std::uint64_t value = 0;
	for (char digit : literal) {
		std::uint64_t next_digit = static_cast<std::uint64_t>(digit - '0');
		if (value > (UINT64_MAX - next_digit) / 10)
			return std::nullopt;
		value = value * 10 + next_digit;
	}
	return value;
}

std::uint64_t max_signed_magnitude(PrimitiveType type)
{
	if (type.bits == 64)
		return 9223372036854775807ULL;
	return (1ULL << (type.bits - 1)) - 1;
}

struct FunctionInfo {
	SourceLocation location;
	PrimitiveType return_type = PrimitiveType{PrimitiveKind::SignedInteger, 32};
	std::vector<PrimitiveType> parameter_types;
};

struct LocalInfo {
	PrimitiveType type;
	bool is_mutable = false;
};

struct GlobalInfo {
	PrimitiveType type;
	bool is_mutable = false;
};

class Analyzer {
public:
	Analyzer(const ast::Module &module, Diagnostics &diagnostics, SemanticOptions options)
		: module_(module), diagnostics_(diagnostics), options_(options)
	{
	}

	bool run()
	{
		build_static_table();
		build_function_table();

		for (const auto &function : module_.functions) {
			if (!function.is_extern)
				analyze_function(function);
		}

		return !diagnostics_.has_errors();
	}

private:
	void build_static_table()
	{
		for (const auto &buffer : module_.static_buffers) {
			if (globals_.find(buffer.name) != globals_.end()) {
				diagnostics_.error(buffer.location, "duplicate static '" + buffer.name + "'");
				continue;
			}

			PrimitiveType element_type = check_type(buffer.element_type);
			if (element_type != u8_type()) {
				diagnostics_.error(buffer.element_type.location,
				                   "static buffers currently require u8 elements");
			}
			if (!buffer.is_mutable)
				diagnostics_.error(buffer.location, "static buffer must be mutable");

			auto length = parse_decimal_magnitude(buffer.length_literal);
			if (!length || *length == 0)
				diagnostics_.error(buffer.location, "static buffer length must be greater than zero");

			globals_[buffer.name] = GlobalInfo{PrimitiveType{PrimitiveKind::Str},
			                                   buffer.is_mutable};
		}

		for (const auto &scalar : module_.static_scalars) {
			if (globals_.find(scalar.name) != globals_.end()) {
				diagnostics_.error(scalar.location, "duplicate static '" + scalar.name + "'");
				continue;
			}

			PrimitiveType type = check_type(scalar.type);
			if (type != i32_type()) {
				diagnostics_.error(scalar.type.location,
				                   "static scalars currently require i32 type");
			}
			if (!scalar.is_mutable)
				diagnostics_.error(scalar.location, "static scalar must be mutable");

			auto initializer = parse_decimal_magnitude(scalar.initializer_literal);
			if (!initializer || !integer_literal_fits(type, static_cast<std::int64_t>(*initializer))) {
				diagnostics_.error(scalar.location,
				                   "static scalar initializer does not fit in i32");
			}

			globals_[scalar.name] = GlobalInfo{type, scalar.is_mutable};
		}
	}

	void build_function_table()
	{
		if (options_.include_stdlib_prelude) {
			for (const auto &function : stdlib::prelude_functions())
				functions_[function.name] =
					FunctionInfo{SourceLocation{}, function.return_type, function.parameters};
		}

		for (const auto &function : module_.functions) {
			if (functions_.find(function.name) != functions_.end() ||
			    globals_.find(function.name) != globals_.end()) {
				diagnostics_.error(function.location, "duplicate function '" + function.name + "'");
				continue;
			}
			FunctionInfo info;
			info.location = function.location;
			info.return_type = check_type(function.return_type);
			for (const auto &parameter : function.parameters)
				info.parameter_types.push_back(check_type(parameter.type));
			functions_[function.name] = std::move(info);
		}
	}

	void analyze_function(const ast::Function &function)
	{
		std::unordered_map<std::string, LocalInfo> locals;

		for (const auto &parameter : function.parameters) {
			PrimitiveType parameter_type = check_type(parameter.type);
			if (!locals.emplace(parameter.name, LocalInfo{parameter_type, false}).second)
				diagnostics_.error(parameter.location, "duplicate local '" + parameter.name + "'");
		}

		PrimitiveType return_type = check_type(function.return_type);

		for (const auto &statement : function.body)
			analyze_statement(return_type, locals, *statement, 0);
	}

	void analyze_statement(PrimitiveType function_return_type,
	                       std::unordered_map<std::string, LocalInfo> &locals,
	                       const ast::Stmt &statement, int loop_depth)
	{
		if (statement.kind == ast::Stmt::Kind::Let) {
			const auto &let = static_cast<const ast::LetStmt &>(statement);
			PrimitiveType let_type = check_type(let.type);
			bool duplicate = locals.find(let.name) != locals.end();
			if (duplicate)
				diagnostics_.error(let.location, "duplicate local '" + let.name + "'");
			// The new binding is inserted after checking the initializer so
			// `let x: i32 = x;` remains an unknown-name error.
			auto initializer_type = check_expr(locals, *let.initializer, let_type);
			if (initializer_type && *initializer_type != let_type) {
				diagnostics_.error(let.location, "initializer type mismatch: expected '" +
				                   format_type(let_type) + "' but got '" +
				                   format_type(*initializer_type) + "'");
			}
			if (!duplicate)
				locals.emplace(let.name, LocalInfo{let_type, let.is_mutable});
			return;
		}

		if (statement.kind == ast::Stmt::Kind::Assign) {
			const auto &assign = static_cast<const ast::AssignStmt &>(statement);
			auto it = locals.find(assign.name);
			if (it == locals.end()) {
				auto global = globals_.find(assign.name);
				if (global == globals_.end()) {
					diagnostics_.error(assign.location, "unknown name '" + assign.name + "'");
					check_expr(locals, *assign.value);
					return;
				}
				if (!global->second.is_mutable)
					diagnostics_.error(assign.location,
					                   "cannot assign to immutable static '" + assign.name + "'");
				auto value_type = check_expr(locals, *assign.value, global->second.type);
				if (value_type && *value_type != global->second.type) {
					diagnostics_.error(assign.location, "assignment type mismatch: expected '" +
					                   format_type(global->second.type) + "' but got '" +
					                   format_type(*value_type) + "'");
				}
				return;
			}
			if (!it->second.is_mutable)
				diagnostics_.error(assign.location,
				                   "cannot assign to immutable local '" + assign.name + "'");
			auto value_type = check_expr(locals, *assign.value, it->second.type);
			if (value_type && *value_type != it->second.type) {
				diagnostics_.error(assign.location, "assignment type mismatch: expected '" +
				                   format_type(it->second.type) + "' but got '" +
				                   format_type(*value_type) + "'");
			}
			return;
		}

		if (statement.kind == ast::Stmt::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ast::IndirectAssignStmt &>(statement);
			auto target_type = check_expr(locals, *assign.target);
			if (!target_type) {
				check_expr(locals, *assign.value);
				return;
			}

			auto target_pointee = pointee_type(*target_type);
			if (!target_pointee) {
				diagnostics_.error(assign.location,
				                   "indirect assignment requires pointer target");
				check_expr(locals, *assign.value);
				return;
			}

			auto value_type = check_expr(locals, *assign.value, *target_pointee);
			if (value_type && *value_type != *target_pointee) {
				diagnostics_.error(assign.location, "assignment type mismatch: expected '" +
				                   format_type(*target_pointee) + "' but got '" +
				                   format_type(*value_type) + "'");
			}
			return;
		}

		if (statement.kind == ast::Stmt::Kind::If) {
			const auto &if_stmt = static_cast<const ast::IfStmt &>(statement);
			auto condition_type = check_expr(locals, *if_stmt.condition, bool_type());
			if (condition_type && *condition_type != bool_type())
				diagnostics_.error(if_stmt.condition->location, "if condition must be bool");

			auto then_locals = locals;
			for (const auto &branch_statement : if_stmt.then_body)
				analyze_statement(function_return_type, then_locals, *branch_statement,
				                  loop_depth);

			auto else_locals = locals;
			for (const auto &branch_statement : if_stmt.else_body)
				analyze_statement(function_return_type, else_locals, *branch_statement,
				                  loop_depth);
			return;
		}

		if (statement.kind == ast::Stmt::Kind::While) {
			const auto &while_stmt = static_cast<const ast::WhileStmt &>(statement);
			auto condition_type = check_expr(locals, *while_stmt.condition, bool_type());
			if (condition_type && *condition_type != bool_type())
				diagnostics_.error(while_stmt.condition->location, "while condition must be bool");

			auto body_locals = locals;
			for (const auto &body_statement : while_stmt.body)
				analyze_statement(function_return_type, body_locals, *body_statement,
				                  loop_depth + 1);
			return;
		}

		if (statement.kind == ast::Stmt::Kind::Break) {
			if (loop_depth == 0)
				diagnostics_.error(statement.location, "break statement outside loop");
			return;
		}

		if (statement.kind == ast::Stmt::Kind::Continue) {
			if (loop_depth == 0)
				diagnostics_.error(statement.location, "continue statement outside loop");
			return;
		}

		if (statement.kind == ast::Stmt::Kind::Expr) {
			const auto &expr_statement = static_cast<const ast::ExprStmt &>(statement);
			check_expr(locals, *expr_statement.value);
			return;
		}

		const auto &ret = static_cast<const ast::ReturnStmt &>(statement);
		auto value_type = check_expr(locals, *ret.value, function_return_type);
		if (value_type && *value_type != function_return_type) {
			diagnostics_.error(ret.location, "return type mismatch: expected '" +
			                   format_type(function_return_type) + "' but got '" +
			                   format_type(*value_type) + "'");
		}
	}

	std::optional<PrimitiveType> check_expr(
		const std::unordered_map<std::string, LocalInfo> &locals, const ast::Expr &expr,
		std::optional<PrimitiveType> expected = std::nullopt)
	{
		// Expected types let unadorned integer literals inherit context from
		// initializers, returns, calls, and unary expressions.
		switch (expr.kind) {
		case ast::Expr::Kind::Integer: {
			const auto &integer = static_cast<const ast::IntegerExpr &>(expr);
			if (expected && is_integer(*expected)) {
				check_integer_literal(expr.location, *expected, integer.literal, false);
				return expected;
			}
			check_integer_literal(expr.location, i32_type(), integer.literal, false);
			return i32_type();
		}
		case ast::Expr::Kind::Bool:
			return PrimitiveType{PrimitiveKind::Bool};
		case ast::Expr::Kind::Char:
			return PrimitiveType{PrimitiveKind::Char};
		case ast::Expr::Kind::String:
			return PrimitiveType{PrimitiveKind::Str};
		case ast::Expr::Kind::Name: {
			const auto &name = static_cast<const ast::NameExpr &>(expr);
			auto it = locals.find(name.name);
			if (it != locals.end())
				return it->second.type;
			auto global = globals_.find(name.name);
			if (global != globals_.end())
				return global->second.type;
			diagnostics_.error(name.location, "unknown name '" + name.name + "'");
			return std::nullopt;
		}
		case ast::Expr::Kind::Binary: {
			const auto &binary = static_cast<const ast::BinaryExpr &>(expr);
			if (is_logical_operator(binary.op))
				return check_logical_binary_expr(locals, binary);

			auto lhs_type = check_expr(locals, *binary.lhs, expected);
			auto rhs_type = check_expr(locals, *binary.rhs, lhs_type ? lhs_type : expected);
			if (!lhs_type || !rhs_type)
				return lhs_type ? lhs_type : rhs_type;
			if (is_comparison_operator(binary.op)) {
				if (!is_integer(*lhs_type) || !is_integer(*rhs_type)) {
					diagnostics_.error(binary.location, "comparison requires integer operands");
					return bool_type();
				}
				if (*lhs_type != *rhs_type)
					diagnostics_.error(binary.location,
					                   "comparison operands must have the same type");
				return bool_type();
			}
			if (is_pointer_arithmetic(binary.op, *lhs_type)) {
				if (!is_integer(*rhs_type)) {
					diagnostics_.error(binary.location,
					                   "pointer arithmetic requires integer offset");
				}
				return pointer_arithmetic_type(*lhs_type);
			}
			if (!is_integer(*lhs_type) || !is_integer(*rhs_type)) {
				diagnostics_.error(binary.location, "arithmetic requires integer operands");
				return *lhs_type;
			}
			if (*lhs_type != *rhs_type) {
				diagnostics_.error(binary.location, "arithmetic operands must have the same type");
				return *lhs_type;
			}
			return lhs_type;
		}
		case ast::Expr::Kind::Cast: {
			const auto &cast = static_cast<const ast::CastExpr &>(expr);
			return check_cast_expr(locals, cast);
		}
		case ast::Expr::Kind::Unary: {
			const auto &unary = static_cast<const ast::UnaryExpr &>(expr);
			return check_unary_expr(locals, unary, expected);
		}
		case ast::Expr::Kind::Call: {
			const auto &call = static_cast<const ast::CallExpr &>(expr);
			auto it = functions_.find(call.callee);
			if (it == functions_.end()) {
				diagnostics_.error(call.location, "unknown function '" + call.callee + "'");
			} else {
				std::size_t expected_count = it->second.parameter_types.size();
				if (expected_count != call.arguments.size()) {
					diagnostics_.error(call.location, "function '" + call.callee + "' expected " +
					                   std::to_string(expected_count) + " arguments but got " +
					                   std::to_string(call.arguments.size()));
				}
				for (std::size_t i = 0; i < call.arguments.size(); ++i) {
					std::optional<PrimitiveType> parameter_type;
					if (i < expected_count)
						parameter_type = it->second.parameter_types[i];
					auto argument_type = check_expr(locals, *call.arguments[i], parameter_type);
					if (parameter_type && argument_type && *argument_type != *parameter_type) {
						diagnostics_.error(call.arguments[i]->location,
						                   "argument type mismatch: expected '" +
						                   format_type(*parameter_type) + "' but got '" +
						                   format_type(*argument_type) + "'");
					}
				}
				return it->second.return_type;
			}
			for (const auto &argument : call.arguments)
				check_expr(locals, *argument);
			return i32_type();
		}
		default:
			break;
		}

		return i32_type();
	}

	std::optional<PrimitiveType> check_unary_expr(
		const std::unordered_map<std::string, LocalInfo> &locals, const ast::UnaryExpr &unary,
		std::optional<PrimitiveType> expected)
	{
		if (unary.op == "!")
			return check_logical_not_expr(locals, unary);
		if (unary.op == "&")
			return check_address_of_expr(locals, unary);
		if (unary.op == "*")
			return check_deref_expr(locals, unary);

		if (unary.op != "-")
			return check_expr(locals, *unary.operand, expected);

		std::optional<PrimitiveType> operand_expected = expected;
		if (expected && !is_signed_integer(*expected)) {
			diagnostics_.error(unary.location, "unary '-' requires a signed integer operand");
			operand_expected = std::nullopt;
		}

		if (unary.operand->kind == ast::Expr::Kind::Integer && operand_expected &&
		    is_signed_integer(*operand_expected)) {
			const auto &integer = static_cast<const ast::IntegerExpr &>(*unary.operand);
			check_integer_literal(unary.location, *operand_expected, integer.literal, true);
			return operand_expected;
		}

		auto operand_type = check_expr(locals, *unary.operand, operand_expected);
		if (!operand_type)
			return std::nullopt;
		if (!is_signed_integer(*operand_type)) {
			diagnostics_.error(unary.location, "unary '-' requires a signed integer operand");
			return operand_type;
		}
		return operand_type;
	}

	std::optional<PrimitiveType> check_logical_binary_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::BinaryExpr &binary)
	{
		auto lhs_type = check_expr(locals, *binary.lhs, bool_type());
		auto rhs_type = check_expr(locals, *binary.rhs, bool_type());
		if (!lhs_type || !rhs_type)
			return bool_type();
		if (*lhs_type != bool_type() || *rhs_type != bool_type())
			diagnostics_.error(binary.location, "logical operator requires bool operands");
		return bool_type();
	}

	bool is_pointer_arithmetic(const std::string &op, PrimitiveType lhs_type) const
	{
		return (is_pointer(lhs_type) || lhs_type.kind == PrimitiveKind::Str) &&
		       (op == "+" || op == "-");
	}

	PrimitiveType pointer_arithmetic_type(PrimitiveType lhs_type) const
	{
		if (lhs_type.kind == PrimitiveKind::Str)
			return pointer_to(u8_type());
		return lhs_type;
	}

	std::optional<PrimitiveType> check_address_of_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::UnaryExpr &unary)
	{
		if (unary.operand->kind != ast::Expr::Kind::Name) {
			diagnostics_.error(unary.location, "address-of requires local name");
			check_expr(locals, *unary.operand);
			return std::nullopt;
		}

		const auto &name = static_cast<const ast::NameExpr &>(*unary.operand);
		auto it = locals.find(name.name);
		if (it == locals.end()) {
			diagnostics_.error(name.location, "unknown name '" + name.name + "'");
			return std::nullopt;
		}
		if (!it->second.is_mutable)
			diagnostics_.error(unary.location,
			                   "address-of requires mutable local '" + name.name + "'");
		return pointer_to(it->second.type);
	}

	std::optional<PrimitiveType> check_deref_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::UnaryExpr &unary)
	{
		auto operand_type = check_expr(locals, *unary.operand);
		if (!operand_type)
			return std::nullopt;
		auto target_type = pointee_type(*operand_type);
		if (!target_type) {
			diagnostics_.error(unary.location, "dereference requires pointer operand");
			return operand_type;
		}
		return target_type;
	}

	std::optional<PrimitiveType> check_cast_expr(
		const std::unordered_map<std::string, LocalInfo> &locals, const ast::CastExpr &cast)
	{
		auto source_type = check_expr(locals, *cast.value);
		PrimitiveType target_type = check_type(cast.target);
		if (!source_type)
			return target_type;
		if (!is_cast_allowed(*source_type, target_type)) {
			diagnostics_.error(cast.location, "cannot cast '" + format_type(*source_type) +
			                   "' to '" + format_type(target_type) + "'");
		}
		return target_type;
	}

	bool is_cast_allowed(PrimitiveType source, PrimitiveType target) const
	{
		if (source == target && source.kind != PrimitiveKind::Str)
			return true;
		if (is_integer(source) && is_integer(target))
			return true;
		if (source.kind == PrimitiveKind::Bool && is_integer(target))
			return true;
		PrimitiveType char_scalar_type{PrimitiveKind::UnsignedInteger, 32};
		if (source.kind == PrimitiveKind::Char && target == char_scalar_type)
			return true;
		return false;
	}

	std::optional<PrimitiveType> check_logical_not_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::UnaryExpr &unary)
	{
		auto operand_type = check_expr(locals, *unary.operand, bool_type());
		if (!operand_type)
			return std::nullopt;
		if (*operand_type != bool_type())
			diagnostics_.error(unary.location, "unary '!' requires a bool operand");
		return bool_type();
	}

	void check_integer_literal(SourceLocation location, PrimitiveType type,
	                           const std::string &literal, bool is_negative)
	{
		bool fits = false;
		auto magnitude = parse_decimal_magnitude(literal);
		if (magnitude) {
			if (is_unsigned_integer(type)) {
				fits = !is_negative && unsigned_integer_literal_fits(type, *magnitude);
			} else if (is_signed_integer(type)) {
				std::uint64_t max = max_signed_magnitude(type);
				fits = is_negative ? *magnitude <= max + 1 : *magnitude <= max;
			}
		}

		if (!fits)
			diagnostics_.error(location, "integer literal does not fit type '" + format_type(type) + "'");
	}

	PrimitiveType check_type(const ast::TypeName &type)
	{
		auto primitive_type = parse_primitive_type(type.name);
		if (!primitive_type) {
			diagnostics_.error(type.location, "unknown type '" + type.name + "'");
			return i32_type();
		}
		return *primitive_type;
	}

	const ast::Module &module_;
	Diagnostics &diagnostics_;
	SemanticOptions options_;
	std::unordered_map<std::string, GlobalInfo> globals_;
	std::unordered_map<std::string, FunctionInfo> functions_;
};

} // namespace

SemanticResult::SemanticResult(bool ok) : ok_(ok) {}

bool SemanticResult::ok() const
{
	return ok_;
}

SemanticResult analyze_module(const ast::Module &module, Diagnostics &diagnostics,
                              SemanticOptions options)
{
	Analyzer analyzer(module, diagnostics, options);
	return SemanticResult(analyzer.run());
}

} // namespace rexc
