// Parser/frontend tests for grammar coverage and AST shape.
//
// These tests exercise parse_source through the generated ANTLR parser and the
// AST builder. They verify that accepted Rexy syntax becomes the compiler-owned
// AST shape expected by sema and later stages.
#include "rexc/ast.hpp"
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

TEST_CASE(parser_preserves_pointer_index_expression_until_lowering)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; return p[0]; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &ret =
	    static_cast<const rexc::ast::ReturnStmt &>(*result.module().functions[0].body[2]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Index);
	const auto &index = static_cast<const rexc::ast::IndexExpr &>(*ret.value);
	REQUIRE_EQ(index.base->kind, rexc::ast::Expr::Kind::Name);
	REQUIRE_EQ(index.index->kind, rexc::ast::Expr::Kind::Integer);
}

TEST_CASE(parser_preserves_index_expression_until_sema)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main(xs: &[i32]) -> i32 { return xs[0]; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().functions[0].parameters[0].type.name,
	           std::string("&[i32]"));
	const auto &ret =
	    static_cast<const rexc::ast::ReturnStmt &>(*result.module().functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Index);
	const auto &index = static_cast<const rexc::ast::IndexExpr &>(*ret.value);
	REQUIRE_EQ(index.base->kind, rexc::ast::Expr::Kind::Name);
	REQUIRE_EQ(index.index->kind, rexc::ast::Expr::Kind::Integer);
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

TEST_CASE(parser_accepts_match_statement_with_literal_and_default_arms)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut value: i32 = 0; match value { 1 => { value = 10; } 2 => { value = 20; } _ => { value = 30; } } return value; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(parser_accepts_match_arm_with_multiple_patterns)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let mut value: i32 = 0; match value { 1 | 2 => { value = 10; } _ => { value = 30; } } return value; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
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

// FE-001 (Phase 1): struct declaration parsing.
//
// These tests assert that the parser accepts struct declarations and exposes
// them on the AST module as StructDecl entries with correctly named fields and
// declared field types. They MUST fail until grammar/Rexy.g4, AstBuilder, and
// the Module AST shape are extended to cover structs.

TEST_CASE(parser_accepts_minimal_struct_declaration)
{
	rexc::SourceFile source("test.rx", "struct Point { x: i32, y: i32 }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(parser_struct_declaration_records_name_and_fields)
{
	rexc::SourceFile source("test.rx", "struct Point { x: i32, y: i32 }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].name, std::string("Point"));
	REQUIRE_EQ(module.structs[0].visibility, rexc::ast::Visibility::Private);
	REQUIRE_EQ(module.structs[0].fields.size(), std::size_t(2));
	REQUIRE_EQ(module.structs[0].fields[0].name, std::string("x"));
	REQUIRE_EQ(module.structs[0].fields[0].type.name, std::string("i32"));
	REQUIRE_EQ(module.structs[0].fields[1].name, std::string("y"));
	REQUIRE_EQ(module.structs[0].fields[1].type.name, std::string("i32"));
}

TEST_CASE(parser_accepts_pub_struct_declaration)
{
	rexc::SourceFile source("test.rx", "pub struct Color { r: u8, g: u8, b: u8 }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].name, std::string("Color"));
	REQUIRE_EQ(module.structs[0].visibility, rexc::ast::Visibility::Public);
	REQUIRE_EQ(module.structs[0].fields.size(), std::size_t(3));
}

TEST_CASE(parser_accepts_struct_with_single_field)
{
	rexc::SourceFile source("test.rx", "struct Wrapper { value: i64 }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].fields.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].fields[0].name, std::string("value"));
	REQUIRE_EQ(module.structs[0].fields[0].type.name, std::string("i64"));
}

TEST_CASE(parser_struct_declaration_records_source_location)
{
	rexc::SourceFile source("test.rx", "struct Point { x: i32, y: i32 }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].location.line, 1u);
	REQUIRE_EQ(module.structs[0].fields[0].location.line, 1u);
	REQUIRE_EQ(module.structs[0].fields[1].location.line, 1u);
}

TEST_CASE(parser_rejects_struct_field_missing_colon)
{
	rexc::SourceFile source("test.rx", "struct Point { x i32, y: i32 }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.has_errors());
}

TEST_CASE(parser_accepts_function_after_struct)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(1));
	REQUIRE_EQ(module.functions.size(), std::size_t(1));
}

// Anti-stub coverage for FE-001: these tests defeat a "recognize 'struct' and
// emit one hardcoded Point" implementation by varying name, field count, field
// types, and ordering across multiple declarations.

TEST_CASE(parser_accepts_multiple_struct_declarations_in_order)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "struct Color { r: u8, g: u8, b: u8 }\n"
	    "struct Flag { on: bool }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(3));
	REQUIRE_EQ(module.structs[0].name, std::string("Point"));
	REQUIRE_EQ(module.structs[1].name, std::string("Color"));
	REQUIRE_EQ(module.structs[2].name, std::string("Flag"));
	REQUIRE_EQ(module.structs[1].fields.size(), std::size_t(3));
	REQUIRE_EQ(module.structs[2].fields.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[2].fields[0].type.name, std::string("bool"));
}

TEST_CASE(parser_preserves_distinct_field_names_and_types)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Mixed { count: u32, label: str, ok: bool, byte: u8 }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].fields.size(), std::size_t(4));
	REQUIRE_EQ(module.structs[0].fields[0].name, std::string("count"));
	REQUIRE_EQ(module.structs[0].fields[0].type.name, std::string("u32"));
	REQUIRE_EQ(module.structs[0].fields[1].name, std::string("label"));
	REQUIRE_EQ(module.structs[0].fields[1].type.name, std::string("str"));
	REQUIRE_EQ(module.structs[0].fields[2].name, std::string("ok"));
	REQUIRE_EQ(module.structs[0].fields[2].type.name, std::string("bool"));
	REQUIRE_EQ(module.structs[0].fields[3].name, std::string("byte"));
	REQUIRE_EQ(module.structs[0].fields[3].type.name, std::string("u8"));
}

TEST_CASE(parser_accepts_struct_field_with_pointer_type)
{
	// Parser should accept *T field types; the meaning of pointers is sema's
	// problem, not the parser's.
	rexc::SourceFile source("test.rx", "struct Node { next: *i32 }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].fields.size(), std::size_t(1));
	REQUIRE(module.structs[0].fields[0].type.name.find('*') != std::string::npos);
}

TEST_CASE(parser_accepts_struct_field_with_unknown_type_name)
{
	// Parser is not a name resolver. Unknown type names are accepted at parse
	// time so sema can report a useful error with the correct span later.
	rexc::SourceFile source("test.rx", "struct Holder { value: NotYetDeclared }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].fields[0].type.name, std::string("NotYetDeclared"));
}

TEST_CASE(parser_struct_inside_inline_module_records_module_path)
{
	rexc::SourceFile source(
	    "test.rx",
	    "mod geom { struct Point { x: i32, y: i32 } }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.structs.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].name, std::string("Point"));
	REQUIRE_EQ(module.structs[0].module_path.size(), std::size_t(1));
	REQUIRE_EQ(module.structs[0].module_path[0], std::string("geom"));
}

TEST_CASE(parser_malformed_struct_does_not_emit_ast)
{
	// Confirms parse failure does not leave a partially-built struct in the
	// AST. Defends against silent half-parsing.
	rexc::SourceFile source("test.rx", "struct Bad { x i32, y: i32 }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.has_errors());
	REQUIRE_EQ(result.module().structs.size(), std::size_t(0));
}

// FE-004 (Phase 1): enum declaration parsing.
//
// These tests assert that the parser accepts enum declarations and exposes
// them on the AST module as EnumDecl entries with named variants and ordered
// tuple-style payload type lists. They MUST fail until grammar/Rexy.g4,
// AstBuilder, and the Module AST shape are extended to cover enums.

TEST_CASE(parser_accepts_minimal_enum_declaration)
{
	rexc::SourceFile source("test.rx", "enum Option { None, Some(i32) }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(parser_enum_declaration_records_name_variants_and_payloads)
{
	rexc::SourceFile source("test.rx", "enum Result { Ok(i32), Err(str) }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.enums.size(), std::size_t(1));
	REQUIRE_EQ(module.enums[0].name, std::string("Result"));
	REQUIRE_EQ(module.enums[0].visibility, rexc::ast::Visibility::Private);
	REQUIRE_EQ(module.enums[0].variants.size(), std::size_t(2));
	REQUIRE_EQ(module.enums[0].variants[0].name, std::string("Ok"));
	REQUIRE_EQ(module.enums[0].variants[0].payload_types.size(), std::size_t(1));
	REQUIRE_EQ(module.enums[0].variants[0].payload_types[0].name, std::string("i32"));
	REQUIRE_EQ(module.enums[0].variants[1].name, std::string("Err"));
	REQUIRE_EQ(module.enums[0].variants[1].payload_types.size(), std::size_t(1));
	REQUIRE_EQ(module.enums[0].variants[1].payload_types[0].name, std::string("str"));
}

TEST_CASE(parser_accepts_multi_argument_handle_type)
{
	rexc::SourceFile source(
	    "test.rx",
	    "enum MyErr { Bad }\n"
	    "fn fallible() -> Result<i32, MyErr> { return Err(Bad()); }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	REQUIRE_EQ(result.module().functions[0].return_type.name,
	           std::string("Result<i32,MyErr>"));
}

TEST_CASE(parser_accepts_pub_enum_and_payloadless_variant)
{
	rexc::SourceFile source("test.rx", "pub enum Token { End, Number(i64) }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	const auto &module = result.module();
	REQUIRE_EQ(module.enums.size(), std::size_t(1));
	REQUIRE_EQ(module.enums[0].name, std::string("Token"));
	REQUIRE_EQ(module.enums[0].visibility, rexc::ast::Visibility::Public);
	REQUIRE_EQ(module.enums[0].variants.size(), std::size_t(2));
	REQUIRE_EQ(module.enums[0].variants[0].name, std::string("End"));
	REQUIRE(module.enums[0].variants[0].payload_types.empty());
}

TEST_CASE(parser_enum_variant_records_multiple_payload_types_in_order)
{
	rexc::SourceFile source(
	    "test.rx",
	    "enum Event { Click(i32, i32), Key(char), Link(*Node, NotYetDeclared), }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.enums.size(), std::size_t(1));
	REQUIRE_EQ(module.enums[0].variants.size(), std::size_t(3));
	const auto &click = module.enums[0].variants[0];
	REQUIRE_EQ(click.name, std::string("Click"));
	REQUIRE_EQ(click.payload_types.size(), std::size_t(2));
	REQUIRE_EQ(click.payload_types[0].name, std::string("i32"));
	REQUIRE_EQ(click.payload_types[1].name, std::string("i32"));
	const auto &link = module.enums[0].variants[2];
	REQUIRE_EQ(link.name, std::string("Link"));
	REQUIRE_EQ(link.payload_types.size(), std::size_t(2));
	REQUIRE_EQ(link.payload_types[0].name, std::string("*Node"));
	REQUIRE_EQ(link.payload_types[1].name, std::string("NotYetDeclared"));
}

TEST_CASE(parser_enum_inside_inline_module_records_module_path)
{
	rexc::SourceFile source("test.rx", "mod ast { enum Expr { Integer(i64), Name(str) } }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.enums.size(), std::size_t(1));
	REQUIRE_EQ(module.enums[0].name, std::string("Expr"));
	REQUIRE_EQ(module.enums[0].module_path.size(), std::size_t(1));
	REQUIRE_EQ(module.enums[0].module_path[0], std::string("ast"));
}

TEST_CASE(parser_enum_declaration_records_source_locations)
{
	rexc::SourceFile source("test.rx", "enum Option { None, Some(i32) }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.enums.size(), std::size_t(1));
	REQUIRE_EQ(module.enums[0].location.line, 1u);
	REQUIRE_EQ(module.enums[0].variants[0].location.line, 1u);
	REQUIRE_EQ(module.enums[0].variants[1].location.line, 1u);
	REQUIRE_EQ(module.enums[0].variants[1].payload_types[0].location.line, 1u);
}

TEST_CASE(parser_malformed_enum_does_not_emit_ast)
{
	rexc::SourceFile source("test.rx", "enum Bad { Some(i32), Broken(, str) }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.has_errors());
	REQUIRE_EQ(result.module().enums.size(), std::size_t(0));
}

TEST_CASE(parser_builds_enum_destructuring_match_pattern)
{
	rexc::SourceFile source(
	    "test.rx",
	    "enum Option { None, Some(i32) }\n"
	    "fn main() -> i32 { let value: Option = Some(42); match value { Some(x) => { return x; } _ => { return 0; } } }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	const auto &match =
	    static_cast<const rexc::ast::MatchStmt &>(*result.module().functions[0].body[1]);
	REQUIRE_EQ(match.arms[0].patterns[0].kind,
	           rexc::ast::MatchPattern::Kind::Variant);
	REQUIRE_EQ(match.arms[0].patterns[0].path.size(), std::size_t(1));
	REQUIRE_EQ(match.arms[0].patterns[0].path[0], std::string("Some"));
	REQUIRE_EQ(match.arms[0].patterns[0].bindings.size(), std::size_t(1));
	REQUIRE_EQ(match.arms[0].patterns[0].bindings[0], std::string("x"));
}

TEST_CASE(parser_builds_struct_destructuring_match_pattern)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn main() -> i32 { let point: Point = Point { x: 1, y: 2 }; match point { Point { x, y } => { return x; } } }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	const auto &match =
	    static_cast<const rexc::ast::MatchStmt &>(*result.module().functions[0].body[1]);
	REQUIRE_EQ(match.arms[0].patterns[0].kind,
	           rexc::ast::MatchPattern::Kind::Struct);
	REQUIRE_EQ(match.arms[0].patterns[0].path[0], std::string("Point"));
	REQUIRE_EQ(match.arms[0].patterns[0].bindings.size(), std::size_t(2));
	REQUIRE_EQ(match.arms[0].patterns[0].bindings[0], std::string("x"));
	REQUIRE_EQ(match.arms[0].patterns[0].bindings[1], std::string("y"));
}

// FE-002a (Phase 1): field-access expression parsing (`p.x`).
//
// These tests assert that `IDENT.IDENT` and chained access shapes are accepted
// and emitted as FieldAccessExpr AST nodes. They MUST fail until the postfix
// grammar rule is extended and AstBuilder produces FieldAccessExpr nodes.

TEST_CASE(parser_accepts_field_access_in_return)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn read_x(p: Point) -> i32 { return p.x; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(parser_field_access_records_base_and_field_name)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn read_x(p: Point) -> i32 { return p.x; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &module = result.module();
	REQUIRE_EQ(module.functions.size(), std::size_t(1));

	const auto &body = module.functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(1));
	REQUIRE_EQ(body[0]->kind, rexc::ast::Stmt::Kind::Return);

	const auto &ret = static_cast<const rexc::ast::ReturnStmt &>(*body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::FieldAccess);
	const auto &field_access = static_cast<const rexc::ast::FieldAccessExpr &>(*ret.value);
	REQUIRE_EQ(field_access.field, std::string("x"));
	REQUIRE_EQ(field_access.base->kind, rexc::ast::Expr::Kind::Name);
	const auto &base = static_cast<const rexc::ast::NameExpr &>(*field_access.base);
	REQUIRE_EQ(base.name, std::string("p"));
}

TEST_CASE(parser_accepts_chained_field_access)
{
	// Defeats a "single-level only" stub. `a.b.c` should parse as
	// FieldAccess{ base = FieldAccess{ base = a, field = b }, field = c }.
	rexc::SourceFile source(
	    "test.rx",
	    "struct Inner { val: i32 }\n"
	    "struct Outer { inner: Inner }\n"
	    "fn read(o: Outer) -> i32 { return o.inner.val; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	const auto &ret = static_cast<const rexc::ast::ReturnStmt &>(*body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::FieldAccess);

	const auto &outer = static_cast<const rexc::ast::FieldAccessExpr &>(*ret.value);
	REQUIRE_EQ(outer.field, std::string("val"));
	REQUIRE_EQ(outer.base->kind, rexc::ast::Expr::Kind::FieldAccess);

	const auto &inner = static_cast<const rexc::ast::FieldAccessExpr &>(*outer.base);
	REQUIRE_EQ(inner.field, std::string("inner"));
	REQUIRE_EQ(inner.base->kind, rexc::ast::Expr::Kind::Name);
}

TEST_CASE(parser_field_access_records_distinct_field_name)
{
	// Anti-stub: defeats "always emit field='x'". Tests a different field name
	// than every other test in this section.
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn read_y(p: Point) -> i32 { return p.y; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	const auto &ret = static_cast<const rexc::ast::ReturnStmt &>(*body[0]);
	const auto &field_access = static_cast<const rexc::ast::FieldAccessExpr &>(*ret.value);
	REQUIRE_EQ(field_access.field, std::string("y"));
}

TEST_CASE(parser_field_access_records_source_location)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn read_x(p: Point) -> i32 { return p.x; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	const auto &ret = static_cast<const rexc::ast::ReturnStmt &>(*body[0]);
	REQUIRE_EQ(ret.value->location.line, 2u);
}

TEST_CASE(parser_field_access_works_in_let_initializer)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn copy_x(p: Point) -> i32 { let v: i32 = p.x; return v; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(2));
	REQUIRE_EQ(body[0]->kind, rexc::ast::Stmt::Kind::Let);
	const auto &let = static_cast<const rexc::ast::LetStmt &>(*body[0]);
	REQUIRE_EQ(let.initializer->kind, rexc::ast::Expr::Kind::FieldAccess);
	const auto &fa = static_cast<const rexc::ast::FieldAccessExpr &>(*let.initializer);
	REQUIRE_EQ(fa.field, std::string("x"));
}

TEST_CASE(parser_rejects_field_access_with_no_field_name)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn bad(p: Point) -> i32 { return p.; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.has_errors());
}

// FE-003b (Phase 1): field assignment via pointer-to-struct.

TEST_CASE(parser_accepts_field_assign_through_pointer_deref)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn write_x(p: *Point) -> i32 { (*p).x = 42; return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(parser_field_assign_records_base_field_value)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn write_y(p: *Point) -> i32 { (*p).y = 7; return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(2));
	REQUIRE_EQ(body[0]->kind, rexc::ast::Stmt::Kind::FieldAssign);
	const auto &fa = static_cast<const rexc::ast::FieldAssignStmt &>(*body[0]);
	REQUIRE_EQ(fa.field, std::string("y"));
}

TEST_CASE(parser_accepts_struct_literal_in_let_initializer)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Point { x: i32, y: i32 }\n"
	    "fn make() -> i32 { let p: Point = Point { x: 40, y: 2 }; return p.y; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

// FE-007 (Phase 1): tuple type and expression parsing.

TEST_CASE(parser_accepts_tuple_type_expression_and_index_access)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { let pair: (i32, bool) = (40, true); return pair.0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

// FE-101 (Phase 2): generic type parameters on `fn` and `struct`.

TEST_CASE(parser_accepts_generic_function_signature)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn id<T>(x: T) -> T { return x; }\n"
	    "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	REQUIRE_EQ(result.module().functions.size(), std::size_t(2));
	REQUIRE_EQ(result.module().functions[0].name, std::string("id"));
	REQUIRE_EQ(result.module().functions[0].generic_parameters.size(), std::size_t(1));
	REQUIRE_EQ(result.module().functions[0].generic_parameters[0], std::string("T"));
}

TEST_CASE(parser_accepts_generic_struct_declaration)
{
	rexc::SourceFile source(
	    "test.rx",
	    "struct Pair<T, U> { first: T, second: U }\n"
	    "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	REQUIRE_EQ(result.module().structs.size(), std::size_t(1));
	REQUIRE_EQ(result.module().structs[0].name, std::string("Pair"));
	REQUIRE_EQ(result.module().structs[0].generic_parameters.size(), std::size_t(2));
	REQUIRE_EQ(result.module().structs[0].generic_parameters[0], std::string("T"));
	REQUIRE_EQ(result.module().structs[0].generic_parameters[1], std::string("U"));
}

TEST_CASE(parser_accepts_function_without_generics_keeps_empty_list)
{
	rexc::SourceFile source(
	    "test.rx", "fn add(a: i32, b: i32) -> i32 { return a + b; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	REQUIRE(result.module().functions[0].generic_parameters.empty());
}

// FE-107: `defer call();` is a top-level statement form. The body must
// be a call expression terminated by `;`.
TEST_CASE(parser_accepts_defer_statement)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn cleanup() -> i32 { return 0; }\n"
	    "fn main() -> i32 {\n"
	    "    defer cleanup();\n"
	    "    return 1;\n"
	    "}\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	const auto &main_fn = result.module().functions[1];
	REQUIRE_EQ(main_fn.name, std::string("main"));
	REQUIRE_EQ(main_fn.body.size(), std::size_t(2));
	REQUIRE(main_fn.body[0]->kind == rexc::ast::Stmt::Kind::Defer);
	const auto &defer = static_cast<const rexc::ast::DeferStmt &>(*main_fn.body[0]);
	REQUIRE(defer.call != nullptr);
	REQUIRE(defer.call->kind == rexc::ast::Expr::Kind::Call);
}

// FE-108: the `let` grammar makes the initializer optional. A bare
// `let mut x: T;` parses to a LetStmt with a null initializer; sema
// will track it as possibly-uninitialized.
TEST_CASE(parser_accepts_let_without_initializer)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 {\n"
	    "    let mut x: i32;\n"
	    "    x = 7;\n"
	    "    return x;\n"
	    "}\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	const auto &main_fn = result.module().functions[0];
	REQUIRE_EQ(main_fn.name, std::string("main"));
	REQUIRE_EQ(main_fn.body.size(), std::size_t(3));
	REQUIRE(main_fn.body[0]->kind == rexc::ast::Stmt::Kind::Let);
	const auto &let = static_cast<const rexc::ast::LetStmt &>(*main_fn.body[0]);
	REQUIRE_EQ(let.name, std::string("x"));
	REQUIRE(let.is_mutable);
	REQUIRE(let.initializer == nullptr);
}
