#pragma once

#include "rexc/source.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rexc::ast {

struct TypeName {
	std::string name;
	SourceLocation location;
};

struct Parameter {
	std::string name;
	TypeName type;
	SourceLocation location;
};

struct Expr {
	enum class Kind { Integer, Bool, Char, String, Name, Unary, Binary, Call };

	Expr(Kind kind, SourceLocation location);
	virtual ~Expr() = default;

	Kind kind;
	SourceLocation location;
};

struct IntegerExpr final : Expr {
	IntegerExpr(SourceLocation location, std::int64_t value);

	std::int64_t value;
};

struct BoolExpr final : Expr {
	BoolExpr(SourceLocation location, bool value);

	bool value;
};

struct CharExpr final : Expr {
	CharExpr(SourceLocation location, char32_t value);

	char32_t value;
};

struct StringExpr final : Expr {
	StringExpr(SourceLocation location, std::string value);

	std::string value;
};

struct NameExpr final : Expr {
	NameExpr(SourceLocation location, std::string name);

	std::string name;
};

struct BinaryExpr final : Expr {
	BinaryExpr(SourceLocation location, std::string op, std::unique_ptr<Expr> lhs,
	           std::unique_ptr<Expr> rhs);

	std::string op;
	std::unique_ptr<Expr> lhs;
	std::unique_ptr<Expr> rhs;
};

struct UnaryExpr final : Expr {
	UnaryExpr(SourceLocation location, std::string op, std::unique_ptr<Expr> operand);

	std::string op;
	std::unique_ptr<Expr> operand;
};

struct CallExpr final : Expr {
	CallExpr(SourceLocation location, std::string callee);

	std::string callee;
	std::vector<std::unique_ptr<Expr>> arguments;
};

struct Stmt {
	enum class Kind { Let, Return };

	Stmt(Kind kind, SourceLocation location);
	virtual ~Stmt() = default;

	Kind kind;
	SourceLocation location;
};

struct LetStmt final : Stmt {
	LetStmt(SourceLocation location, std::string name, TypeName type,
	        std::unique_ptr<Expr> initializer);

	std::string name;
	TypeName type;
	std::unique_ptr<Expr> initializer;
};

struct ReturnStmt final : Stmt {
	ReturnStmt(SourceLocation location, std::unique_ptr<Expr> value);

	std::unique_ptr<Expr> value;
};

struct Function {
	bool is_extern = false;
	std::string name;
	std::vector<Parameter> parameters;
	TypeName return_type;
	std::vector<std::unique_ptr<Stmt>> body;
	SourceLocation location;
};

struct Module {
	std::vector<Function> functions;
};

} // namespace rexc::ast
