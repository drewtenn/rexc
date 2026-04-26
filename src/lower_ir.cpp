// AST-to-IR lowering.
//
// This file converts semantically checked source syntax into the smaller typed
// representation consumed by codegen. It resolves type names into PrimitiveType
// values, preserves expression and statement structure needed by the backend,
// and maps source locals/parameters into IR declarations while assuming sema
// has already rejected invalid names, types, and control flow.
#include "rexc/lower_ir.hpp"
#include "rexc/types.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

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

bool is_logical_operator(const std::string &op)
{
	return op == "&&" || op == "||";
}

ir::Type lower_type(const ast::TypeName &type)
{
	auto primitive_type = parse_primitive_type(type.name);
	if (!primitive_type)
		throw std::runtime_error("unknown primitive type in IR lowering: " + type.name);
	return *primitive_type;
}

struct FunctionInfo {
	ir::Type return_type = i32_type();
	std::vector<ir::Type> parameter_types;
};

class Lowerer {
public:
	explicit Lowerer(const ast::Module &module) : module_(module) {}

	ir::Module run()
	{
		build_function_table();

		ir::Module lowered;
		for (const auto &function : module_.functions)
			lowered.functions.push_back(lower_function(function));
		return lowered;
	}

private:
	using Locals = std::unordered_map<std::string, ir::Type>;

	void build_function_table()
	{
		for (const auto &function : module_.functions) {
			FunctionInfo info;
			info.return_type = lower_type(function.return_type);
			for (const auto &parameter : function.parameters)
				info.parameter_types.push_back(lower_type(parameter.type));
			functions_[function.name] = std::move(info);
		}
	}

	std::unique_ptr<ir::Value> lower_expr(
		const ast::Expr &expr, const Locals &locals,
		std::optional<ir::Type> expected = std::nullopt)
	{
		// Sema has already proven types match; this mirrors its expected-type
		// flow so literal IR nodes keep their resolved primitive type.
		switch (expr.kind) {
		case ast::Expr::Kind::Integer: {
			const auto &integer = static_cast<const ast::IntegerExpr &>(expr);
			ir::Type type = expected && is_integer(*expected) ? *expected : i32_type();
			return std::make_unique<ir::IntegerValue>(type, integer.literal, false);
		}
		case ast::Expr::Kind::Bool: {
			const auto &boolean = static_cast<const ast::BoolExpr &>(expr);
			return std::make_unique<ir::BoolValue>(boolean.value);
		}
		case ast::Expr::Kind::Char: {
			const auto &character = static_cast<const ast::CharExpr &>(expr);
			return std::make_unique<ir::CharValue>(character.value);
		}
		case ast::Expr::Kind::String: {
			const auto &string = static_cast<const ast::StringExpr &>(expr);
			return std::make_unique<ir::StringValue>(string.value);
		}
		case ast::Expr::Kind::Name: {
			const auto &name = static_cast<const ast::NameExpr &>(expr);
			auto it = locals.find(name.name);
			if (it == locals.end())
				throw std::runtime_error("unknown local in IR lowering: " + name.name);
			return std::make_unique<ir::LocalValue>(name.name, it->second);
		}
		case ast::Expr::Kind::Binary: {
			const auto &binary = static_cast<const ast::BinaryExpr &>(expr);
			if (is_logical_operator(binary.op)) {
				return std::make_unique<ir::BinaryValue>(
					binary.op, lower_expr(*binary.lhs, locals, bool_type()),
					lower_expr(*binary.rhs, locals, bool_type()), bool_type());
			}

			auto lhs = lower_expr(*binary.lhs, locals, expected);
			ir::Type operand_type = lhs->type;
			auto rhs = lower_expr(*binary.rhs, locals, operand_type);
			ir::Type type = is_comparison_operator(binary.op) ? bool_type() : operand_type;
			return std::make_unique<ir::BinaryValue>(binary.op, std::move(lhs),
			                                         std::move(rhs), type);
		}
		case ast::Expr::Kind::Cast: {
			const auto &cast = static_cast<const ast::CastExpr &>(expr);
			return std::make_unique<ir::CastValue>(
				lower_expr(*cast.value, locals), lower_type(cast.target));
		}
		case ast::Expr::Kind::Unary:
			return lower_unary(static_cast<const ast::UnaryExpr &>(expr), locals, expected);
		case ast::Expr::Kind::Call: {
			const auto &call_expr = static_cast<const ast::CallExpr &>(expr);
			auto it = functions_.find(call_expr.callee);
			if (it == functions_.end())
				throw std::runtime_error("unknown function in IR lowering: " +
				                         call_expr.callee);

			auto call = std::make_unique<ir::CallValue>(call_expr.callee, it->second.return_type);
			for (std::size_t i = 0; i < call_expr.arguments.size(); ++i) {
				std::optional<ir::Type> parameter_type;
				if (i < it->second.parameter_types.size())
					parameter_type = it->second.parameter_types[i];
				call->arguments.push_back(
					lower_expr(*call_expr.arguments[i], locals, parameter_type));
			}
			return call;
		}
		}

		throw std::runtime_error("unexpected expression in IR lowering");
	}

	std::unique_ptr<ir::Value> lower_unary(
		const ast::UnaryExpr &unary, const Locals &locals,
		std::optional<ir::Type> expected)
	{
		auto operand = lower_expr(*unary.operand, locals,
		                          unary.op == "*" ? std::nullopt : expected);
		ir::Type type = operand->type;
		if (unary.op == "&") {
			type = pointer_to(type);
		} else if (unary.op == "*") {
			auto target_type = pointee_type(type);
			if (!target_type)
				throw std::runtime_error("dereference of non-pointer in IR lowering");
			type = *target_type;
		}
		return std::make_unique<ir::UnaryValue>(unary.op, std::move(operand), type);
	}

	std::vector<std::unique_ptr<ir::Statement>> lower_statements(
		const std::vector<std::unique_ptr<ast::Stmt>> &statements,
		ir::Type function_return_type, Locals locals)
	{
		std::vector<std::unique_ptr<ir::Statement>> lowered;
		for (const auto &statement : statements)
			lowered.push_back(lower_statement(*statement, function_return_type, locals));
		return lowered;
	}

	std::unique_ptr<ir::Statement> lower_statement(const ast::Stmt &statement,
	                                               ir::Type function_return_type,
	                                               Locals &locals)
	{
		if (statement.kind == ast::Stmt::Kind::Let) {
			const auto &let = static_cast<const ast::LetStmt &>(statement);
			ir::Type let_type = lower_type(let.type);
			auto initializer = lower_expr(*let.initializer, locals, let_type);
			locals[let.name] = let_type;
			return std::make_unique<ir::LetStatement>(let.name, std::move(initializer));
		}

		if (statement.kind == ast::Stmt::Kind::Assign) {
			const auto &assign = static_cast<const ast::AssignStmt &>(statement);
			auto it = locals.find(assign.name);
			if (it == locals.end())
				throw std::runtime_error("unknown local in IR lowering: " + assign.name);
			return std::make_unique<ir::AssignStatement>(
				assign.name, lower_expr(*assign.value, locals, it->second));
		}

		if (statement.kind == ast::Stmt::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ast::IndirectAssignStmt &>(statement);
			auto target = lower_expr(*assign.target, locals);
			auto target_pointee = pointee_type(target->type);
			if (!target_pointee)
				throw std::runtime_error("indirect assignment through non-pointer in IR lowering");
			return std::make_unique<ir::IndirectAssignStatement>(
				std::move(target), lower_expr(*assign.value, locals, *target_pointee));
		}

		if (statement.kind == ast::Stmt::Kind::If) {
			const auto &if_stmt = static_cast<const ast::IfStmt &>(statement);
			auto condition = lower_expr(*if_stmt.condition, locals, bool_type());
			return std::make_unique<ir::IfStatement>(
				std::move(condition),
				lower_statements(if_stmt.then_body, function_return_type, locals),
				lower_statements(if_stmt.else_body, function_return_type, locals));
		}

		if (statement.kind == ast::Stmt::Kind::While) {
			const auto &while_stmt = static_cast<const ast::WhileStmt &>(statement);
			auto condition = lower_expr(*while_stmt.condition, locals, bool_type());
			return std::make_unique<ir::WhileStatement>(
				std::move(condition),
				lower_statements(while_stmt.body, function_return_type, locals));
		}

		if (statement.kind == ast::Stmt::Kind::Break)
			return std::make_unique<ir::BreakStatement>();

		if (statement.kind == ast::Stmt::Kind::Continue)
			return std::make_unique<ir::ContinueStatement>();

		if (statement.kind == ast::Stmt::Kind::Expr)
			throw std::runtime_error("expression statements are not supported in IR lowering yet");

		const auto &ret = static_cast<const ast::ReturnStmt &>(statement);
		return std::make_unique<ir::ReturnStatement>(
			lower_expr(*ret.value, locals, function_return_type));
	}

	ir::Function lower_function(const ast::Function &function)
	{
		ir::Function lowered;
		lowered.is_extern = function.is_extern;
		lowered.name = function.name;
		lowered.return_type = lower_type(function.return_type);

		Locals locals;
		for (const auto &parameter : function.parameters) {
			ir::Type parameter_type = lower_type(parameter.type);
			lowered.parameters.push_back({parameter.name, parameter_type});
			locals[parameter.name] = parameter_type;
		}

		for (const auto &statement : function.body) {
			lowered.body.push_back(
				lower_statement(*statement, lowered.return_type, locals));
		}

		return lowered;
	}

	const ast::Module &module_;
	std::unordered_map<std::string, FunctionInfo> functions_;
};

} // namespace

ir::Module lower_to_ir(const ast::Module &module)
{
	return Lowerer(module).run();
}

} // namespace rexc
