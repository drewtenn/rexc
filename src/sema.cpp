// Semantic analysis for the parsed Rexc AST.
//
// The parser proves only that source text has the shape of the grammar. This
// stage proves the program is meaningful enough to lower: functions and locals
// are declared once, names resolve, calls match signatures, expressions have
// compatible primitive types, integer literals fit their target types, and
// break/continue appear only inside loops.
#include "rexc/sema.hpp"
#include "rexc/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

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

bool is_comparison_operator(const std::string &op)
{
	return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
	       op == ">=";
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
	const ast::Function *function = nullptr;
};

struct LocalInfo {
	PrimitiveType type;
	bool is_mutable = false;
};

class Analyzer {
public:
	Analyzer(const ast::Module &module, Diagnostics &diagnostics)
		: module_(module), diagnostics_(diagnostics)
	{
	}

	bool run()
	{
		build_function_table();

		for (const auto &function : module_.functions) {
			if (!function.is_extern)
				analyze_function(function);
		}

		return !diagnostics_.has_errors();
	}

private:
	void build_function_table()
	{
		for (const auto &function : module_.functions) {
			if (functions_.find(function.name) != functions_.end()) {
				diagnostics_.error(function.location, "duplicate function '" + function.name + "'");
				continue;
			}
			functions_[function.name] = FunctionInfo{&function};
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
				diagnostics_.error(assign.location, "unknown name '" + assign.name + "'");
				check_expr(locals, *assign.value);
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
			if (it == locals.end()) {
				diagnostics_.error(name.location, "unknown name '" + name.name + "'");
				return std::nullopt;
			}
			return it->second.type;
		}
		case ast::Expr::Kind::Binary: {
			const auto &binary = static_cast<const ast::BinaryExpr &>(expr);
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
				std::size_t expected_count = it->second.function->parameters.size();
				if (expected_count != call.arguments.size()) {
					diagnostics_.error(call.location, "function '" + call.callee + "' expected " +
					                   std::to_string(expected_count) + " arguments but got " +
					                   std::to_string(call.arguments.size()));
				}
				for (std::size_t i = 0; i < call.arguments.size(); ++i) {
					std::optional<PrimitiveType> parameter_type;
					if (i < expected_count)
						parameter_type = check_type(it->second.function->parameters[i].type);
					auto argument_type = check_expr(locals, *call.arguments[i], parameter_type);
					if (parameter_type && argument_type && *argument_type != *parameter_type) {
						diagnostics_.error(call.arguments[i]->location,
						                   "argument type mismatch: expected '" +
						                   format_type(*parameter_type) + "' but got '" +
						                   format_type(*argument_type) + "'");
					}
				}
				return check_type(it->second.function->return_type);
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
	std::unordered_map<std::string, FunctionInfo> functions_;
};

} // namespace

SemanticResult::SemanticResult(bool ok) : ok_(ok) {}

bool SemanticResult::ok() const
{
	return ok_;
}

SemanticResult analyze_module(const ast::Module &module, Diagnostics &diagnostics)
{
	Analyzer analyzer(module, diagnostics);
	return SemanticResult(analyzer.run());
}

} // namespace rexc
