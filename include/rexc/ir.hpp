#pragma once

#include <memory>
#include <string>
#include <vector>

namespace rexc::ir {

enum class Type { I32 };

struct Value {
	enum class Kind { Integer, Local, Binary, Call };

	Value(Kind kind, Type type);
	virtual ~Value() = default;

	Kind kind;
	Type type;
};

struct IntegerValue final : Value {
	explicit IntegerValue(int value);

	int value;
};

struct LocalValue final : Value {
	explicit LocalValue(std::string name);

	std::string name;
};

struct BinaryValue final : Value {
	BinaryValue(std::string op, std::unique_ptr<Value> lhs, std::unique_ptr<Value> rhs);

	std::string op;
	std::unique_ptr<Value> lhs;
	std::unique_ptr<Value> rhs;
};

struct CallValue final : Value {
	explicit CallValue(std::string callee);

	std::string callee;
	std::vector<std::unique_ptr<Value>> arguments;
};

struct Statement {
	enum class Kind { Let, Return };

	explicit Statement(Kind kind);
	virtual ~Statement() = default;

	Kind kind;
};

struct LetStatement final : Statement {
	LetStatement(std::string name, std::unique_ptr<Value> value);

	std::string name;
	std::unique_ptr<Value> value;
};

struct ReturnStatement final : Statement {
	explicit ReturnStatement(std::unique_ptr<Value> value);

	std::unique_ptr<Value> value;
};

struct Parameter {
	std::string name;
	Type type = Type::I32;
};

struct Function {
	bool is_extern = false;
	std::string name;
	std::vector<Parameter> parameters;
	Type return_type = Type::I32;
	std::vector<std::unique_ptr<Statement>> body;
};

struct Module {
	std::vector<Function> functions;
};

} // namespace rexc::ir
