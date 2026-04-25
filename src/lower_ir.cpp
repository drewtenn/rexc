#include "rexc/lower_ir.hpp"
#include "rexc/types.hpp"

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace rexc {
namespace {

ir::Type lower_type(const ast::TypeName &type)
{
	auto primitive_type = parse_primitive_type(type.name);
	if (!primitive_type || *primitive_type != PrimitiveType{PrimitiveKind::SignedInteger, 32})
		throw std::runtime_error("type is not supported by current IR lowering: " + type.name);
	return ir::Type::I32;
}

std::string decimal_literal_magnitude(const std::string &literal)
{
	std::size_t first_non_zero = literal.find_first_not_of('0');
	return first_non_zero == std::string::npos ? "0" : literal.substr(first_non_zero);
}

bool decimal_literal_exceeds(const std::string &literal, const std::string &max)
{
	std::string magnitude = decimal_literal_magnitude(literal);
	if (magnitude.size() != max.size())
		return magnitude.size() > max.size();
	return magnitude > max;
}

bool decimal_literal_exceeds_current_integer_value(const std::string &literal)
{
	return decimal_literal_exceeds(literal, std::to_string(std::numeric_limits<int>::max()));
}

bool decimal_literal_exceeds_current_negative_integer_value(const std::string &literal)
{
	return decimal_literal_exceeds(literal, "2147483648");
}

bool decimal_literal_is_current_integer_min_magnitude(const std::string &literal)
{
	return decimal_literal_magnitude(literal) == "2147483648";
}

void guard_current_integer_value_literal(const ast::IntegerExpr &integer, bool is_negative)
{
	if (is_negative && !decimal_literal_exceeds_current_negative_integer_value(integer.literal))
		return;
	if (!is_negative && !decimal_literal_exceeds_current_integer_value(integer.literal))
		return;

	std::string literal = is_negative ? "-" + integer.literal : integer.literal;
	throw std::runtime_error("integer literal is not supported by current IR lowering: " +
	                         literal);
}

std::unique_ptr<ir::Value> lower_expr(const ast::Expr &expr)
{
	switch (expr.kind) {
	case ast::Expr::Kind::Integer: {
		const auto &integer = static_cast<const ast::IntegerExpr &>(expr);
		guard_current_integer_value_literal(integer, false);
		return std::make_unique<ir::IntegerValue>(integer.value);
	}
	case ast::Expr::Kind::Bool:
		throw std::runtime_error("literal type is not supported by current IR lowering: bool");
	case ast::Expr::Kind::Char:
		throw std::runtime_error("literal type is not supported by current IR lowering: char");
	case ast::Expr::Kind::String:
		throw std::runtime_error("literal type is not supported by current IR lowering: str");
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
		if (unary.op == "-") {
			if (unary.operand->kind == ast::Expr::Kind::Integer) {
				const auto &integer = static_cast<const ast::IntegerExpr &>(*unary.operand);
				guard_current_integer_value_literal(integer, true);
				if (decimal_literal_is_current_integer_min_magnitude(integer.literal))
					return std::make_unique<ir::IntegerValue>(std::numeric_limits<int>::min());
			}
			return std::make_unique<ir::BinaryValue>("-", std::make_unique<ir::IntegerValue>(0),
			                                         lower_expr(*unary.operand));
		}
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
		auto initializer = lower_expr(*let.initializer);
		(void)lower_type(let.type);
		return std::make_unique<ir::LetStatement>(let.name, std::move(initializer));
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
