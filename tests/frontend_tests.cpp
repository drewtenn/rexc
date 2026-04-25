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
