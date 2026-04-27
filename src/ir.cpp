// Constructors for Rexy's typed intermediate representation.
//
// The IR is the backend-facing form produced after semantic analysis. It keeps
// resolved primitive types on values, locals, functions, branches, and calls so
// code generation no longer has to interpret parser spelling or AST-only
// structure.
#include "rexc/ir.hpp"

#include <utility>

namespace rexc::ir {

Value::Value(Kind kind, Type type) : kind(kind), type(type) {}

IntegerValue::IntegerValue(Type type, std::string literal, bool is_negative)
	: Value(Kind::Integer, type), literal(std::move(literal)), is_negative(is_negative)
{
}

BoolValue::BoolValue(bool value)
	: Value(Kind::Bool, PrimitiveType{PrimitiveKind::Bool}), value(value)
{
}

CharValue::CharValue(char32_t value)
	: Value(Kind::Char, PrimitiveType{PrimitiveKind::Char}), value(value)
{
}

StringValue::StringValue(std::string value)
	: Value(Kind::String, PrimitiveType{PrimitiveKind::Str}), value(std::move(value))
{
}

LocalValue::LocalValue(std::string name, Type type)
	: Value(Kind::Local, type), name(std::move(name))
{
}

GlobalValue::GlobalValue(std::string name, Type type)
	: Value(Kind::Global, type), name(std::move(name))
{
}

UnaryValue::UnaryValue(std::string op, std::unique_ptr<Value> operand, Type type)
	: Value(Kind::Unary, type), op(std::move(op)), operand(std::move(operand))
{
}

BinaryValue::BinaryValue(std::string op, std::unique_ptr<Value> lhs,
                         std::unique_ptr<Value> rhs, Type type)
	: Value(Kind::Binary, type), op(std::move(op)), lhs(std::move(lhs)),
	  rhs(std::move(rhs))
{
}

CastValue::CastValue(std::unique_ptr<Value> value, Type type)
	: Value(Kind::Cast, type), value(std::move(value))
{
}

CallValue::CallValue(std::string callee, Type type)
	: Value(Kind::Call, type), callee(std::move(callee))
{
}

Statement::Statement(Kind kind) : kind(kind) {}

LetStatement::LetStatement(std::string name, std::unique_ptr<Value> value)
	: Statement(Kind::Let), name(std::move(name)), value(std::move(value))
{
}

AssignStatement::AssignStatement(std::string name, std::unique_ptr<Value> value)
	: Statement(Kind::Assign), name(std::move(name)), value(std::move(value))
{
}

IndirectAssignStatement::IndirectAssignStatement(std::unique_ptr<Value> target,
                                                 std::unique_ptr<Value> value)
	: Statement(Kind::IndirectAssign), target(std::move(target)), value(std::move(value))
{
}

ExprStatement::ExprStatement(std::unique_ptr<Value> value)
	: Statement(Kind::Expr), value(std::move(value))
{
}

ReturnStatement::ReturnStatement(std::unique_ptr<Value> value)
	: Statement(Kind::Return), value(std::move(value))
{
}

IfStatement::IfStatement(std::unique_ptr<Value> condition,
                         std::vector<std::unique_ptr<Statement>> then_body,
                         std::vector<std::unique_ptr<Statement>> else_body)
	: Statement(Kind::If), condition(std::move(condition)),
	  then_body(std::move(then_body)), else_body(std::move(else_body))
{
}

WhileStatement::WhileStatement(std::unique_ptr<Value> condition,
                               std::vector<std::unique_ptr<Statement>> body)
	: Statement(Kind::While), condition(std::move(condition)), body(std::move(body))
{
}

BreakStatement::BreakStatement()
	: Statement(Kind::Break)
{
}

ContinueStatement::ContinueStatement()
	: Statement(Kind::Continue)
{
}

} // namespace rexc::ir
