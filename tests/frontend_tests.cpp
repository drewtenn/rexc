// Parser/frontend tests for grammar coverage and AST shape.
//
// These tests exercise parse_source through the generated ANTLR parser and the
// AST builder. They verify that accepted Rexc syntax becomes the compiler-owned
// AST shape expected by sema and later stages.
#include "rexc/parse.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <string>

TEST_CASE(parser_accepts_minimal_function)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	REQUIRE_EQ(result.module().functions.size(), 1u);
	REQUIRE_EQ(result.module().functions[0].name, std::string("main"));
}

TEST_CASE(parser_builds_ast_for_add_function)
{
	rexc::SourceFile source("test.rx",
		"fn add(a: i32, b: i32) -> i32 { return a + b; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.functions.size(), 1u);
	REQUIRE_EQ(module.functions[0].name, std::string("add"));
	REQUIRE_EQ(module.functions[0].parameters.size(), 2u);
	REQUIRE_EQ(module.functions[0].return_type.name, std::string("i32"));
}

TEST_CASE(parser_rejects_malformed_function)
{
	rexc::SourceFile source("test.rx", "fn main( -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.has_errors());
}

TEST_CASE(parser_accepts_all_core_type_names)
{
	rexc::SourceFile source("test.rx",
		"fn f(a: i8, b: i16, c: i32, d: i64, e: u8, f: u16, g: u32, h: u64, i: bool, j: char, k: str) -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto result = rexc::parse_source(source, diagnostics);
	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().functions[0].parameters.size(), 11u);
	REQUIRE_EQ(result.module().functions[0].parameters[0].type.name, std::string("i8"));
	REQUIRE_EQ(result.module().functions[0].parameters[10].type.name, std::string("str"));
}

TEST_CASE(parser_accepts_bool_char_string_and_unary_literals)
{
	rexc::SourceFile source("test.rx",
		"fn main() -> i32 { let a: i32 = -12; let b: bool = true; let c: char = '\\n'; let d: str = \"hi\"; return a; }\n");
	rexc::Diagnostics diagnostics;
	auto result = rexc::parse_source(source, diagnostics);
	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), 5u);

	const auto &a = static_cast<const rexc::ast::LetStmt &>(*body[0]);
	REQUIRE_EQ(a.initializer->kind, rexc::ast::Expr::Kind::Unary);
	const auto &unary = static_cast<const rexc::ast::UnaryExpr &>(*a.initializer);
	REQUIRE_EQ(unary.op, std::string("-"));
	REQUIRE_EQ(unary.operand->kind, rexc::ast::Expr::Kind::Integer);
	const auto &integer = static_cast<const rexc::ast::IntegerExpr &>(*unary.operand);
	REQUIRE_EQ(integer.value, 12);

	const auto &b = static_cast<const rexc::ast::LetStmt &>(*body[1]);
	REQUIRE_EQ(b.initializer->kind, rexc::ast::Expr::Kind::Bool);
	const auto &boolean = static_cast<const rexc::ast::BoolExpr &>(*b.initializer);
	REQUIRE(boolean.value);

	const auto &c = static_cast<const rexc::ast::LetStmt &>(*body[2]);
	REQUIRE_EQ(c.initializer->kind, rexc::ast::Expr::Kind::Char);
	const auto &character = static_cast<const rexc::ast::CharExpr &>(*c.initializer);
	REQUIRE_EQ(character.value, U'\n');

	const auto &d = static_cast<const rexc::ast::LetStmt &>(*body[3]);
	REQUIRE_EQ(d.initializer->kind, rexc::ast::Expr::Kind::String);
	const auto &string = static_cast<const rexc::ast::StringExpr &>(*d.initializer);
	REQUIRE_EQ(string.value, std::string("hi"));
}

TEST_CASE(parser_accepts_comparisons)
{
	rexc::SourceFile source("test.rx", "fn main() -> bool { return 1 <= 2; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &ret =
	    static_cast<const rexc::ast::ReturnStmt &>(*result.module().functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Binary);
	const auto &binary = static_cast<const rexc::ast::BinaryExpr &>(*ret.value);
	REQUIRE_EQ(binary.op, std::string("<="));
}

TEST_CASE(parser_accepts_boolean_operators_with_precedence)
{
	rexc::SourceFile source(
	    "test.rx", "fn main() -> bool { return !true || false && 1 < 2; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &ret =
	    static_cast<const rexc::ast::ReturnStmt &>(*result.module().functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Binary);
	const auto &logical_or = static_cast<const rexc::ast::BinaryExpr &>(*ret.value);
	REQUIRE_EQ(logical_or.op, std::string("||"));
	REQUIRE_EQ(logical_or.lhs->kind, rexc::ast::Expr::Kind::Unary);
	const auto &logical_not = static_cast<const rexc::ast::UnaryExpr &>(*logical_or.lhs);
	REQUIRE_EQ(logical_not.op, std::string("!"));
	REQUIRE_EQ(logical_or.rhs->kind, rexc::ast::Expr::Kind::Binary);
	const auto &logical_and = static_cast<const rexc::ast::BinaryExpr &>(*logical_or.rhs);
	REQUIRE_EQ(logical_and.op, std::string("&&"));
	REQUIRE_EQ(logical_and.rhs->kind, rexc::ast::Expr::Kind::Binary);
	const auto &comparison = static_cast<const rexc::ast::BinaryExpr &>(*logical_and.rhs);
	REQUIRE_EQ(comparison.op, std::string("<"));
}

TEST_CASE(parser_accepts_if_else_statements)
{
	rexc::SourceFile source(
	    "test.rx", "fn main() -> i32 { if 1 < 2 { return 1; } else { return 0; } }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &stmt = *result.module().functions[0].body[0];
	REQUIRE_EQ(stmt.kind, rexc::ast::Stmt::Kind::If);
	const auto &if_stmt = static_cast<const rexc::ast::IfStmt &>(stmt);
	REQUIRE_EQ(if_stmt.condition->kind, rexc::ast::Expr::Kind::Binary);
	REQUIRE_EQ(if_stmt.then_body.size(), std::size_t(1));
	REQUIRE_EQ(if_stmt.else_body.size(), std::size_t(1));
}

TEST_CASE(parser_accepts_mutable_locals_assignment_and_while)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut x: i32 = 0; while x < 3 { x = x + 1; } return x; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(3));

	const auto &let = static_cast<const rexc::ast::LetStmt &>(*body[0]);
	REQUIRE(let.is_mutable);

	REQUIRE_EQ(body[1]->kind, rexc::ast::Stmt::Kind::While);
	const auto &while_stmt = static_cast<const rexc::ast::WhileStmt &>(*body[1]);
	REQUIRE_EQ(while_stmt.condition->kind, rexc::ast::Expr::Kind::Binary);
	REQUIRE_EQ(while_stmt.body.size(), std::size_t(1));
	REQUIRE_EQ(while_stmt.body[0]->kind, rexc::ast::Stmt::Kind::Assign);
	const auto &assign = static_cast<const rexc::ast::AssignStmt &>(*while_stmt.body[0]);
	REQUIRE_EQ(assign.name, std::string("x"));
	REQUIRE_EQ(assign.value->kind, rexc::ast::Expr::Kind::Binary);
}

TEST_CASE(parser_accepts_break_and_continue_statements)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { while true { continue; break; } return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(2));
	REQUIRE_EQ(body[0]->kind, rexc::ast::Stmt::Kind::While);

	const auto &while_stmt = static_cast<const rexc::ast::WhileStmt &>(*body[0]);
	REQUIRE_EQ(while_stmt.body.size(), std::size_t(2));
	REQUIRE_EQ(while_stmt.body[0]->kind, rexc::ast::Stmt::Kind::Continue);
	REQUIRE_EQ(while_stmt.body[1]->kind, rexc::ast::Stmt::Kind::Break);
}
