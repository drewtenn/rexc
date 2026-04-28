#pragma once

// Source-level syntax tree produced by the parser and checked by sema.
#include "rexc/source.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rexc::ast {

enum class Visibility { Private, Public };

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
	enum class Kind { Integer, Bool, Char, String, Name, Unary, Binary, Cast, Call };

	Expr(Kind kind, SourceLocation location);
	virtual ~Expr() = default;

	Kind kind;
	SourceLocation location;
};

struct IntegerExpr final : Expr {
	IntegerExpr(SourceLocation location, std::int64_t value, std::string literal);

	std::int64_t value;
	std::string literal;
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

struct CastExpr final : Expr {
	CastExpr(SourceLocation location, std::unique_ptr<Expr> value, TypeName target);

	std::unique_ptr<Expr> value;
	TypeName target;
};

struct UnaryExpr final : Expr {
	UnaryExpr(SourceLocation location, std::string op, std::unique_ptr<Expr> operand);

	std::string op;
	std::unique_ptr<Expr> operand;
};

struct CallExpr final : Expr {
	CallExpr(SourceLocation location, std::string callee);
	CallExpr(SourceLocation location, std::vector<std::string> callee_path);

	std::string callee;
	std::vector<std::string> callee_path;
	std::vector<std::unique_ptr<Expr>> arguments;
};

struct Stmt {
	enum class Kind { Let, Assign, IndirectAssign, Expr, Return, If, While, For, Break, Continue };

	Stmt(Kind kind, SourceLocation location);
	virtual ~Stmt() = default;

	Kind kind;
	SourceLocation location;
};

struct LetStmt final : Stmt {
	LetStmt(SourceLocation location, bool is_mutable, std::string name, TypeName type,
	        std::unique_ptr<Expr> initializer);

	bool is_mutable = false;
	std::string name;
	TypeName type;
	std::unique_ptr<Expr> initializer;
};

struct AssignStmt final : Stmt {
	AssignStmt(SourceLocation location, std::string name, std::unique_ptr<Expr> value);

	std::string name;
	std::unique_ptr<Expr> value;
};

struct IndirectAssignStmt final : Stmt {
	IndirectAssignStmt(SourceLocation location, std::unique_ptr<Expr> target,
	                   std::unique_ptr<Expr> value);

	std::unique_ptr<Expr> target;
	std::unique_ptr<Expr> value;
};

struct ExprStmt final : Stmt {
	ExprStmt(SourceLocation location, std::unique_ptr<Expr> value);

	std::unique_ptr<Expr> value;
};

struct ReturnStmt final : Stmt {
	ReturnStmt(SourceLocation location, std::unique_ptr<Expr> value);

	std::unique_ptr<Expr> value;
};

struct IfStmt final : Stmt {
	IfStmt(SourceLocation location, std::unique_ptr<Expr> condition,
	       std::vector<std::unique_ptr<Stmt>> then_body,
	       std::vector<std::unique_ptr<Stmt>> else_body);

	std::unique_ptr<Expr> condition;
	std::vector<std::unique_ptr<Stmt>> then_body;
	std::vector<std::unique_ptr<Stmt>> else_body;
};

struct WhileStmt final : Stmt {
	WhileStmt(SourceLocation location, std::unique_ptr<Expr> condition,
	          std::vector<std::unique_ptr<Stmt>> body);

	std::unique_ptr<Expr> condition;
	std::vector<std::unique_ptr<Stmt>> body;
};

struct ForStmt final : Stmt {
	ForStmt(SourceLocation location, std::unique_ptr<Stmt> initializer,
	        std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> increment,
	        std::vector<std::unique_ptr<Stmt>> body);

	std::unique_ptr<Stmt> initializer;
	std::unique_ptr<Expr> condition;
	std::unique_ptr<Stmt> increment;
	std::vector<std::unique_ptr<Stmt>> body;
};

struct BreakStmt final : Stmt {
	explicit BreakStmt(SourceLocation location);
};

struct ContinueStmt final : Stmt {
	explicit ContinueStmt(SourceLocation location);
};

struct Function {
	bool is_extern = false;
	Visibility visibility = Visibility::Private;
	std::string name;
	std::vector<Parameter> parameters;
	TypeName return_type;
	std::vector<std::unique_ptr<Stmt>> body;
	SourceLocation location;
	std::vector<std::string> module_path;
};

struct StaticBuffer {
	struct Initializer {
		enum class Kind { Integer, Bool, Char, String };

		Kind kind = Kind::Integer;
		std::string literal;
		bool bool_value = false;
		char32_t char_value = U'\0';
		bool is_negative = false;
		SourceLocation location;
	};

	bool is_mutable = false;
	Visibility visibility = Visibility::Private;
	std::string name;
	TypeName element_type;
	std::string length_literal;
	std::vector<Initializer> initializers;
	SourceLocation location;
	std::vector<std::string> module_path;
};

struct StaticScalar {
	bool is_mutable = false;
	Visibility visibility = Visibility::Private;
	std::string name;
	TypeName type;
	std::string initializer_literal;
	SourceLocation location;
	std::vector<std::string> module_path;
};

struct UseDecl {
	std::vector<std::string> module_path;
	std::vector<std::string> import_path;
	SourceLocation location;
};

struct ModuleDecl {
	Visibility visibility = Visibility::Private;
	std::vector<std::string> module_path;
	bool is_file_backed = false;
	SourceLocation location;
};

struct Module {
	std::vector<ModuleDecl> modules;
	std::vector<UseDecl> uses;
	std::vector<StaticBuffer> static_buffers;
	std::vector<StaticScalar> static_scalars;
	std::vector<Function> functions;
};

} // namespace rexc::ast
