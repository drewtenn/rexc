#include "rexc/diagnostics.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <string>

static rexc::SemanticResult analyze(const std::string &text, rexc::Diagnostics &diagnostics)
{
	rexc::SourceFile source("test.rx", text);
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	return rexc::analyze_module(parsed.module(), diagnostics);
}

TEST_CASE(sema_accepts_valid_add_program)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn add(a: i32, b: i32) -> i32 { return a + b; }\n", diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_duplicate_functions)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { return 0; }\nfn main() -> i32 { return 1; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate function 'main'") != std::string::npos);
}

TEST_CASE(sema_rejects_unknown_call)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { return missing(); }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown function 'missing'") != std::string::npos);
}

TEST_CASE(sema_rejects_wrong_arity)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn add(a: i32) -> i32 { return a; }\nfn main() -> i32 { return add(1, 2); }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("expected 1 arguments but got 2") != std::string::npos);
}

TEST_CASE(sema_rejects_unknown_local)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { return value; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown name 'value'") != std::string::npos);
}

TEST_CASE(sema_rejects_unknown_local_inside_unary)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { return -missing; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown name 'missing'") != std::string::npos);
}
