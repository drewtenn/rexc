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
	REQUIRE_EQ(result.module().functions[0].body.size(), 5u);
}
