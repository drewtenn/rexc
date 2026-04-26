// AST node construction for Rexc's source-level tree.
//
// The parser builds these nodes after ANTLR has accepted the grammar. The AST
// preserves source structure and locations, but it deliberately does not know
// whether names resolve, types match, or control-flow statements are legal.
// Ownership of child expressions/statements lives in unique_ptr and vector
// fields declared in include/rexc/ast.hpp.
#include "rexc/ast.hpp"

#include <sstream>
#include <utility>

namespace rexc::ast {
namespace {

std::string join_path(const std::vector<std::string> &segments)
{
	std::ostringstream joined;
	for (std::size_t i = 0; i < segments.size(); ++i) {
		if (i > 0)
			joined << "::";
		joined << segments[i];
	}
	return joined.str();
}

} // namespace

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

CastExpr::CastExpr(SourceLocation location, std::unique_ptr<Expr> value, TypeName target)
	: Expr(Kind::Cast, std::move(location)), value(std::move(value)),
	  target(std::move(target))
{
}

UnaryExpr::UnaryExpr(SourceLocation location, std::string op, std::unique_ptr<Expr> operand)
	: Expr(Kind::Unary, std::move(location)), op(std::move(op)),
	  operand(std::move(operand))
{
}

CallExpr::CallExpr(SourceLocation location, std::string callee)
	: Expr(Kind::Call, std::move(location)), callee(std::move(callee)),
	  callee_path{this->callee}
{
}

CallExpr::CallExpr(SourceLocation location, std::vector<std::string> callee_path)
	: Expr(Kind::Call, std::move(location)), callee(join_path(callee_path)),
	  callee_path(std::move(callee_path))
{
}

Stmt::Stmt(Kind kind, SourceLocation location)
	: kind(kind), location(std::move(location))
{
}

LetStmt::LetStmt(SourceLocation location, bool is_mutable, std::string name, TypeName type,
                 std::unique_ptr<Expr> initializer)
	: Stmt(Kind::Let, std::move(location)), is_mutable(is_mutable),
	  name(std::move(name)), type(std::move(type)), initializer(std::move(initializer))
{
}

AssignStmt::AssignStmt(SourceLocation location, std::string name, std::unique_ptr<Expr> value)
	: Stmt(Kind::Assign, std::move(location)), name(std::move(name)),
	  value(std::move(value))
{
}

IndirectAssignStmt::IndirectAssignStmt(SourceLocation location, std::unique_ptr<Expr> target,
                                       std::unique_ptr<Expr> value)
	: Stmt(Kind::IndirectAssign, std::move(location)), target(std::move(target)),
	  value(std::move(value))
{
}

ExprStmt::ExprStmt(SourceLocation location, std::unique_ptr<Expr> value)
	: Stmt(Kind::Expr, std::move(location)), value(std::move(value))
{
}

ReturnStmt::ReturnStmt(SourceLocation location, std::unique_ptr<Expr> value)
	: Stmt(Kind::Return, std::move(location)), value(std::move(value))
{
}

IfStmt::IfStmt(SourceLocation location, std::unique_ptr<Expr> condition,
               std::vector<std::unique_ptr<Stmt>> then_body,
               std::vector<std::unique_ptr<Stmt>> else_body)
	: Stmt(Kind::If, std::move(location)), condition(std::move(condition)),
	  then_body(std::move(then_body)), else_body(std::move(else_body))
{
}

WhileStmt::WhileStmt(SourceLocation location, std::unique_ptr<Expr> condition,
                     std::vector<std::unique_ptr<Stmt>> body)
	: Stmt(Kind::While, std::move(location)), condition(std::move(condition)),
	  body(std::move(body))
{
}

BreakStmt::BreakStmt(SourceLocation location)
	: Stmt(Kind::Break, std::move(location))
{
}

ContinueStmt::ContinueStmt(SourceLocation location)
	: Stmt(Kind::Continue, std::move(location))
{
}

} // namespace rexc::ast
