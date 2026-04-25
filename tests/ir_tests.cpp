#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

TEST_CASE(lowering_preserves_function_signature)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 42; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions.size(), 1u);
	REQUIRE_EQ(module.functions[0].name, std::string("main"));
	REQUIRE(module.functions[0].return_type == rexc::ir::Type::I32);
	REQUIRE_EQ(module.functions[0].body.size(), 1u);
}
