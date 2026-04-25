#pragma once

// Typed intermediate representation consumed by the x86 backend.
#include "rexc/types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rexc::ir {

using Type = PrimitiveType;

struct Value {
	enum class Kind { Integer, Bool, Char, String, Local, Unary, Binary, Call };

	Value(Kind kind, Type type);
	virtual ~Value() = default;

	Kind kind;
	Type type;
};

struct IntegerValue final : Value {
	IntegerValue(Type type, std::string literal, bool is_negative);

	std::string literal;
	bool is_negative = false;
};

struct BoolValue final : Value {
	explicit BoolValue(bool value);

	bool value = false;
};

struct CharValue final : Value {
	explicit CharValue(char32_t value);

	char32_t value = U'\0';
};

struct StringValue final : Value {
	explicit StringValue(std::string value);

	std::string value;
};

struct LocalValue final : Value {
	LocalValue(std::string name, Type type);

	std::string name;
};

struct UnaryValue final : Value {
	UnaryValue(std::string op, std::unique_ptr<Value> operand, Type type);

	std::string op;
	std::unique_ptr<Value> operand;
};

struct BinaryValue final : Value {
	BinaryValue(std::string op, std::unique_ptr<Value> lhs, std::unique_ptr<Value> rhs,
	            Type type);

	std::string op;
	std::unique_ptr<Value> lhs;
	std::unique_ptr<Value> rhs;
};

struct CallValue final : Value {
	CallValue(std::string callee, Type type);

	std::string callee;
	std::vector<std::unique_ptr<Value>> arguments;
};

struct Statement {
	enum class Kind { Let, Assign, Return, If, While, Break, Continue };

	explicit Statement(Kind kind);
	virtual ~Statement() = default;

	Kind kind;
};

struct LetStatement final : Statement {
	LetStatement(std::string name, std::unique_ptr<Value> value);

	std::string name;
	std::unique_ptr<Value> value;
};

struct AssignStatement final : Statement {
	AssignStatement(std::string name, std::unique_ptr<Value> value);

	std::string name;
	std::unique_ptr<Value> value;
};

struct ReturnStatement final : Statement {
	explicit ReturnStatement(std::unique_ptr<Value> value);

	std::unique_ptr<Value> value;
};

struct IfStatement final : Statement {
	IfStatement(std::unique_ptr<Value> condition,
	            std::vector<std::unique_ptr<Statement>> then_body,
	            std::vector<std::unique_ptr<Statement>> else_body);

	std::unique_ptr<Value> condition;
	std::vector<std::unique_ptr<Statement>> then_body;
	std::vector<std::unique_ptr<Statement>> else_body;
};

struct WhileStatement final : Statement {
	WhileStatement(std::unique_ptr<Value> condition,
	               std::vector<std::unique_ptr<Statement>> body);

	std::unique_ptr<Value> condition;
	std::vector<std::unique_ptr<Statement>> body;
};

struct BreakStatement final : Statement {
	BreakStatement();
};

struct ContinueStatement final : Statement {
	ContinueStatement();
};

struct Parameter {
	std::string name;
	Type type = PrimitiveType{PrimitiveKind::SignedInteger, 32};
};

struct Function {
	bool is_extern = false;
	std::string name;
	std::vector<Parameter> parameters;
	Type return_type = PrimitiveType{PrimitiveKind::SignedInteger, 32};
	std::vector<std::unique_ptr<Statement>> body;
};

struct Module {
	std::vector<Function> functions;
};

} // namespace rexc::ir
