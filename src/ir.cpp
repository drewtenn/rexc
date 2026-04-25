#include "rexc/ir.hpp"

#include <utility>

namespace rexc::ir {

Value::Value(Kind kind, Type type) : kind(kind), type(type) {}

IntegerValue::IntegerValue(int value) : Value(Kind::Integer, Type::I32), value(value) {}

LocalValue::LocalValue(std::string name)
	: Value(Kind::Local, Type::I32), name(std::move(name))
{
}

BinaryValue::BinaryValue(std::string op, std::unique_ptr<Value> lhs,
                         std::unique_ptr<Value> rhs)
	: Value(Kind::Binary, Type::I32), op(std::move(op)), lhs(std::move(lhs)),
	  rhs(std::move(rhs))
{
}

CallValue::CallValue(std::string callee)
	: Value(Kind::Call, Type::I32), callee(std::move(callee))
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
