#include "rexc/lower_ir.hpp"

#include <memory>
#include <utility>

namespace rexc {
namespace {

ir::Type lower_type(const ast::TypeName &)
{
	return ir::Type::I32;
}

std::unique_ptr<ir::Value> lower_expr(const ast::Expr &expr)
{
	switch (expr.kind) {
	case ast::Expr::Kind::Integer: {
		const auto &integer = static_cast<const ast::IntegerExpr &>(expr);
		return std::make_unique<ir::IntegerValue>(integer.value);
	}
	case ast::Expr::Kind::Name: {
		const auto &name = static_cast<const ast::NameExpr &>(expr);
		return std::make_unique<ir::LocalValue>(name.name);
	}
	case ast::Expr::Kind::Binary: {
		const auto &binary = static_cast<const ast::BinaryExpr &>(expr);
		return std::make_unique<ir::BinaryValue>(binary.op, lower_expr(*binary.lhs),
		                                         lower_expr(*binary.rhs));
	}
	case ast::Expr::Kind::Unary: {
		const auto &unary = static_cast<const ast::UnaryExpr &>(expr);
		if (unary.op == "-")
			return std::make_unique<ir::BinaryValue>("-", std::make_unique<ir::IntegerValue>(0),
			                                         lower_expr(*unary.operand));
		return lower_expr(*unary.operand);
	}
	case ast::Expr::Kind::Call: {
		const auto &call_expr = static_cast<const ast::CallExpr &>(expr);
		auto call = std::make_unique<ir::CallValue>(call_expr.callee);
		for (const auto &argument : call_expr.arguments)
			call->arguments.push_back(lower_expr(*argument));
		return call;
	}
	default:
		break;
	}

	return std::make_unique<ir::IntegerValue>(0);
}

std::unique_ptr<ir::Statement> lower_statement(const ast::Stmt &statement)
{
	if (statement.kind == ast::Stmt::Kind::Let) {
		const auto &let = static_cast<const ast::LetStmt &>(statement);
		return std::make_unique<ir::LetStatement>(let.name, lower_expr(*let.initializer));
	}

	const auto &ret = static_cast<const ast::ReturnStmt &>(statement);
	return std::make_unique<ir::ReturnStatement>(lower_expr(*ret.value));
}

ir::Function lower_function(const ast::Function &function)
{
	ir::Function lowered;
	lowered.is_extern = function.is_extern;
	lowered.name = function.name;
	lowered.return_type = lower_type(function.return_type);

	for (const auto &parameter : function.parameters)
		lowered.parameters.push_back({parameter.name, lower_type(parameter.type)});

	for (const auto &statement : function.body)
		lowered.body.push_back(lower_statement(*statement));

	return lowered;
}

} // namespace

ir::Module lower_to_ir(const ast::Module &module)
{
	ir::Module lowered;

	for (const auto &function : module.functions)
		lowered.functions.push_back(lower_function(function));

	return lowered;
}

} // namespace rexc
