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

CallValue::CallValue(std::string callee, Type type)
	: Value(Kind::Call, type), callee(std::move(callee))
{
}

Statement::Statement(Kind kind) : kind(kind) {}

LetStatement::LetStatement(std::string name, std::unique_ptr<Value> value)
	: Statement(Kind::Let), name(std::move(name)), value(std::move(value))
{
}

ReturnStatement::ReturnStatement(std::unique_ptr<Value> value)
	: Statement(Kind::Return), value(std::move(value))
{
}

} // namespace rexc::ir
