#include "rexc/ast.hpp"

#include <utility>

namespace rexc::ast {

Expr::Expr(Kind kind, SourceLocation location)
	: kind(kind), location(std::move(location))
{
}

IntegerExpr::IntegerExpr(SourceLocation location, std::int64_t value, std::string literal)
	: Expr(Kind::Integer, std::move(location)), value(value),
	  literal(std::move(literal))
{
}

BoolExpr::BoolExpr(SourceLocation location, bool value)
	: Expr(Kind::Bool, std::move(location)), value(value)
{
}

CharExpr::CharExpr(SourceLocation location, char32_t value)
	: Expr(Kind::Char, std::move(location)), value(value)
{
}

StringExpr::StringExpr(SourceLocation location, std::string value)
	: Expr(Kind::String, std::move(location)), value(std::move(value))
{
}

NameExpr::NameExpr(SourceLocation location, std::string name)
	: Expr(Kind::Name, std::move(location)), name(std::move(name))
{
}

BinaryExpr::BinaryExpr(SourceLocation location, std::string op,
                       std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs)
	: Expr(Kind::Binary, std::move(location)), op(std::move(op)),
	  lhs(std::move(lhs)), rhs(std::move(rhs))
{
}

UnaryExpr::UnaryExpr(SourceLocation location, std::string op, std::unique_ptr<Expr> operand)
	: Expr(Kind::Unary, std::move(location)), op(std::move(op)),
	  operand(std::move(operand))
{
}

CallExpr::CallExpr(SourceLocation location, std::string callee)
	: Expr(Kind::Call, std::move(location)), callee(std::move(callee))
{
}

Stmt::Stmt(Kind kind, SourceLocation location)
	: kind(kind), location(std::move(location))
{
}

LetStmt::LetStmt(SourceLocation location, std::string name, TypeName type,
                 std::unique_ptr<Expr> initializer)
	: Stmt(Kind::Let, std::move(location)), name(std::move(name)),
	  type(std::move(type)), initializer(std::move(initializer))
{
}

ReturnStmt::ReturnStmt(SourceLocation location, std::unique_ptr<Expr> value)
	: Stmt(Kind::Return, std::move(location)), value(std::move(value))
{
}

} // namespace rexc::ast
