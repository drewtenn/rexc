// Parser/frontend tests for grammar coverage and AST shape.
//
// These tests exercise parse_source through the generated ANTLR parser and the
// AST builder. They verify that accepted Rexy syntax becomes the compiler-owned
// AST shape expected by sema and later stages.
#include "rexc/parse.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path make_temp_dir(const std::string &name)
{
	auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	auto path = std::filesystem::temp_directory_path() /
	            ("rexc-" + name + "-" + std::to_string(stamp));
	std::filesystem::create_directories(path);
	return path;
}

void write_text(const std::filesystem::path &path, const std::string &text)
{
	std::filesystem::create_directories(path.parent_path());
	std::ofstream output(path);
	REQUIRE(output.is_open());
	output << text;
}

} // namespace

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

TEST_CASE(parser_builds_module_paths_and_qualified_calls)
{
	rexc::SourceFile source(
	    "test.rx",
	    "mod math { fn add(a: i32, b: i32) -> i32 { return a + b; } }\n"
	    "fn main() -> i32 { return math::add(1, 2); }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.functions.size(), std::size_t(2));
	REQUIRE_EQ(module.functions[0].module_path.size(), std::size_t(1));
	REQUIRE_EQ(module.functions[0].module_path[0], std::string("math"));
	REQUIRE_EQ(module.functions[0].name, std::string("add"));

	const auto &ret = static_cast<const rexc::ast::ReturnStmt &>(*module.functions[1].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Call);
	const auto &call = static_cast<const rexc::ast::CallExpr &>(*ret.value);
	REQUIRE_EQ(call.callee_path.size(), std::size_t(2));
	REQUIRE_EQ(call.callee_path[0], std::string("math"));
	REQUIRE_EQ(call.callee_path[1], std::string("add"));
}

TEST_CASE(parser_builds_use_imports)
{
	rexc::SourceFile source("test.rx",
		"use math::add;\n"
		"mod math { fn add(a: i32, b: i32) -> i32 { return a + b; } }\n"
		"fn main() -> i32 { return add(1, 2); }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().uses.size(), std::size_t(1));
	REQUIRE_EQ(result.module().uses[0].module_path.size(), std::size_t(0));
	REQUIRE_EQ(result.module().uses[0].import_path.size(), std::size_t(2));
	REQUIRE_EQ(result.module().uses[0].import_path[0], std::string("math"));
	REQUIRE_EQ(result.module().uses[0].import_path[1], std::string("add"));
}

TEST_CASE(parser_builds_visibility_and_file_module_declarations)
{
	rexc::SourceFile source("test.rx",
		"pub mod math;\n"
		"pub static mut VALUE: i32 = 1;\n"
		"pub fn main() -> i32 { return VALUE; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().modules.size(), std::size_t(1));
	REQUIRE_EQ(result.module().modules[0].visibility, rexc::ast::Visibility::Public);
	REQUIRE(result.module().modules[0].is_file_backed);
	REQUIRE_EQ(result.module().modules[0].module_path.size(), std::size_t(1));
	REQUIRE_EQ(result.module().modules[0].module_path[0], std::string("math"));
	REQUIRE_EQ(result.module().static_scalars[0].visibility, rexc::ast::Visibility::Public);
	REQUIRE_EQ(result.module().functions[0].visibility, rexc::ast::Visibility::Public);
}

TEST_CASE(parser_rebases_source_to_module_path)
{
	rexc::SourceFile source("math.rx",
		"use inner::value;\n"
		"pub fn add(a: i32, b: i32) -> i32 { return a + b; }\n");
	rexc::Diagnostics diagnostics;
	rexc::ParseOptions options;
	options.module_path.push_back("math");

	auto result = rexc::parse_source(source, diagnostics, options);

	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().uses[0].module_path.size(), std::size_t(1));
	REQUIRE_EQ(result.module().uses[0].module_path[0], std::string("math"));
	REQUIRE_EQ(result.module().functions[0].module_path.size(), std::size_t(1));
	REQUIRE_EQ(result.module().functions[0].module_path[0], std::string("math"));
}

TEST_CASE(parser_loads_file_backed_sibling_module)
{
	auto dir = make_temp_dir("sibling-module");
	write_text(dir / "main.rx",
	           "pub mod math;\n"
	           "fn main() -> i32 { return math::add(1, 2); }\n");
	write_text(dir / "math.rx",
	           "pub fn add(a: i32, b: i32) -> i32 { return a + b; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_file_tree((dir / "main.rx").string(), diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	REQUIRE_EQ(result.module().functions.size(), std::size_t(2));
	REQUIRE_EQ(result.module().functions[0].module_path.size(), std::size_t(0));
	REQUIRE_EQ(result.module().functions[1].module_path.size(), std::size_t(1));
	REQUIRE_EQ(result.module().functions[1].module_path[0], std::string("math"));
	std::filesystem::remove_all(dir);
}

TEST_CASE(parser_loads_file_backed_module_from_package_path)
{
	auto dir = make_temp_dir("package-entry");
	auto package = make_temp_dir("package-root");
	write_text(dir / "main.rx",
	           "pub mod math;\n"
	           "fn main() -> i32 { return math::add(1, 2); }\n");
	write_text(package / "math.rx",
	           "pub fn add(a: i32, b: i32) -> i32 { return a + b; }\n");
	rexc::Diagnostics diagnostics;
	rexc::ModuleLoadOptions options;
	options.package_paths.push_back(package.string());

	auto result = rexc::parse_file_tree((dir / "main.rx").string(), diagnostics, options);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	REQUIRE_EQ(result.module().functions.size(), std::size_t(2));
	REQUIRE_EQ(result.module().functions[1].module_path[0], std::string("math"));
	std::filesystem::remove_all(dir);
	std::filesystem::remove_all(package);
}

TEST_CASE(parser_reports_missing_file_backed_module)
{
	auto dir = make_temp_dir("missing-module");
	write_text(dir / "main.rx", "mod missing;\nfn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_file_tree((dir / "main.rx").string(), diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("module file not found 'missing'") !=
	        std::string::npos);
	REQUIRE(diagnostics.format().find("missing.rx") != std::string::npos);
	std::filesystem::remove_all(dir);
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

TEST_CASE(parser_accepts_static_mut_byte_buffer)
{
	rexc::SourceFile source(
	    "test.rx",
	    "static mut READ_LINE_BUFFER: [u8; 1024];\nfn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().static_buffers.size(), std::size_t(1));
	const auto &buffer = result.module().static_buffers[0];
	REQUIRE(buffer.is_mutable);
	REQUIRE_EQ(buffer.name, std::string("READ_LINE_BUFFER"));
	REQUIRE_EQ(buffer.element_type.name, std::string("u8"));
	REQUIRE_EQ(buffer.length_literal, std::string("1024"));
}

TEST_CASE(parser_accepts_initialized_static_array)
{
	rexc::SourceFile source(
	    "test.rx",
	    "static MONTHS: [str; 3] = [\"Jan\", \"Feb\", \"Mar\"];\n"
	    "fn main() -> str { return MONTHS[1]; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().static_buffers.size(), std::size_t(1));
	const auto &buffer = result.module().static_buffers[0];
	REQUIRE(!buffer.is_mutable);
	REQUIRE_EQ(buffer.name, std::string("MONTHS"));
	REQUIRE_EQ(buffer.element_type.name, std::string("str"));
	REQUIRE_EQ(buffer.length_literal, std::string("3"));
	REQUIRE_EQ(buffer.initializers.size(), std::size_t(3));
	REQUIRE_EQ(buffer.initializers[0].literal, std::string("Jan"));
}

TEST_CASE(parser_accepts_static_mut_i32_scalar)
{
	rexc::SourceFile source(
	    "test.rx",
	    "static mut ALLOC_OFFSET: i32 = 0;\nfn main() -> i32 { return ALLOC_OFFSET; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().static_scalars.size(), std::size_t(1));
	const auto &scalar = result.module().static_scalars[0];
	REQUIRE(scalar.is_mutable);
	REQUIRE_EQ(scalar.name, std::string("ALLOC_OFFSET"));
	REQUIRE_EQ(scalar.type.name, std::string("i32"));
	REQUIRE_EQ(scalar.initializer_literal, std::string("0"));
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

TEST_CASE(parser_accepts_remainder_operator)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 7 % 3; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &ret =
	    static_cast<const rexc::ast::ReturnStmt &>(*result.module().functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Binary);
	const auto &binary = static_cast<const rexc::ast::BinaryExpr &>(*ret.value);
	REQUIRE_EQ(binary.op, std::string("%"));
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

TEST_CASE(parser_accepts_cast_expressions)
{
	rexc::SourceFile source("test.rx", "fn main() -> u32 { return 'A' as u32; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &ret =
	    static_cast<const rexc::ast::ReturnStmt &>(*result.module().functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Cast);
	const auto &cast = static_cast<const rexc::ast::CastExpr &>(*ret.value);
	REQUIRE_EQ(cast.target.name, std::string("u32"));
	REQUIRE_EQ(cast.value->kind, rexc::ast::Expr::Kind::Char);
}

TEST_CASE(parser_accepts_pointer_types_and_unary_pointer_expressions)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; return *p; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(3));
	const auto &p = static_cast<const rexc::ast::LetStmt &>(*body[1]);
	REQUIRE_EQ(p.type.name, std::string("*i32"));
	REQUIRE_EQ(p.initializer->kind, rexc::ast::Expr::Kind::Unary);
	const auto &address = static_cast<const rexc::ast::UnaryExpr &>(*p.initializer);
	REQUIRE_EQ(address.op, std::string("&"));
	REQUIRE_EQ(address.operand->kind, rexc::ast::Expr::Kind::Name);

	const auto &ret = static_cast<const rexc::ast::ReturnStmt &>(*body[2]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Unary);
	const auto &deref = static_cast<const rexc::ast::UnaryExpr &>(*ret.value);
	REQUIRE_EQ(deref.op, std::string("*"));
	REQUIRE_EQ(deref.operand->kind, rexc::ast::Expr::Kind::Name);
}

TEST_CASE(parser_accepts_indirect_assignment)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; *(p + 1) = 9; return x; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(4));
	REQUIRE_EQ(body[2]->kind, rexc::ast::Stmt::Kind::IndirectAssign);
	const auto &assign = static_cast<const rexc::ast::IndirectAssignStmt &>(*body[2]);
	REQUIRE_EQ(assign.target->kind, rexc::ast::Expr::Kind::Binary);
	REQUIRE_EQ(assign.value->kind, rexc::ast::Expr::Kind::Integer);
}

TEST_CASE(parser_desugars_index_expression_to_pointer_add_deref)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; return p[0]; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &ret =
	    static_cast<const rexc::ast::ReturnStmt &>(*result.module().functions[0].body[2]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Unary);
	const auto &deref = static_cast<const rexc::ast::UnaryExpr &>(*ret.value);
	REQUIRE_EQ(deref.op, std::string("*"));
	REQUIRE_EQ(deref.operand->kind, rexc::ast::Expr::Kind::Binary);
	const auto &add = static_cast<const rexc::ast::BinaryExpr &>(*deref.operand);
	REQUIRE_EQ(add.op, std::string("+"));
	REQUIRE_EQ(add.lhs->kind, rexc::ast::Expr::Kind::Name);
	REQUIRE_EQ(add.rhs->kind, rexc::ast::Expr::Kind::Integer);
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

TEST_CASE(parser_accepts_else_if_statements)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { if 1 < 2 { return 1; } else if 2 < 3 { return 2; } else { return 0; } }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &stmt = *result.module().functions[0].body[0];
	REQUIRE_EQ(stmt.kind, rexc::ast::Stmt::Kind::If);
	const auto &if_stmt = static_cast<const rexc::ast::IfStmt &>(stmt);
	REQUIRE_EQ(if_stmt.else_body.size(), std::size_t(1));
	REQUIRE_EQ(if_stmt.else_body[0]->kind, rexc::ast::Stmt::Kind::If);
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

TEST_CASE(parser_accepts_for_loop)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut total: i32 = 0; for let mut i: i32 = 0; i < 3; i = i + 1 { total = total + i; } return total; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(parser_accepts_prefix_and_postfix_increment_decrement)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut i: i32 = 0; ++i; i++; --i; i--; return i; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(6));
	REQUIRE_EQ(body[1]->kind, rexc::ast::Stmt::Kind::Expr);
	REQUIRE_EQ(body[2]->kind, rexc::ast::Stmt::Kind::Expr);
}

TEST_CASE(parser_accepts_increment_in_for_header)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut total: i32 = 0; for let mut i: i32 = 0; i < 3; i++ { total = total + i; } return total; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().functions[0].body[1]->kind, rexc::ast::Stmt::Kind::For);
}

TEST_CASE(parser_accepts_parenthesized_for_loop_header)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut total: i32 = 0; for (let mut i: i32 = 0; i < 3; i = i + 1) { total = total + i; } return total; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
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

TEST_CASE(parser_accepts_call_statement)
{
	rexc::SourceFile source("test.rx",
		"fn main() -> i32 { println(\"hello\"); return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(2));
	REQUIRE_EQ(body[0]->kind, rexc::ast::Stmt::Kind::Expr);
	const auto &stmt = static_cast<const rexc::ast::ExprStmt &>(*body[0]);
	REQUIRE_EQ(stmt.value->kind, rexc::ast::Expr::Kind::Call);
	const auto &call = static_cast<const rexc::ast::CallExpr &>(*stmt.value);
	REQUIRE_EQ(call.callee, std::string("println"));
	REQUIRE_EQ(call.arguments.size(), std::size_t(1));
	REQUIRE_EQ(call.arguments[0]->kind, rexc::ast::Expr::Kind::String);
	const auto &argument = static_cast<const rexc::ast::StringExpr &>(*call.arguments[0]);
	REQUIRE_EQ(argument.value, std::string("hello"));
}

TEST_CASE(parser_rejects_non_call_expression_statement)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { 1 + 2; return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.has_errors());
}
