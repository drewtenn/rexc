// Unit coverage for source locations and diagnostic formatting.
//
// These tests protect the user-facing error surface: byte offsets should map
// to stable line/column positions, and rendered diagnostics should be suitable
// for CLI output.
#include "rexc/diagnostics.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <string>

TEST_CASE(source_maps_offsets_to_line_and_column)
{
	rexc::SourceFile source("test.rx", "fn main()\n  return 1;\n");

	auto loc = source.location_at(12);

	REQUIRE_EQ(loc.file, std::string("test.rx"));
	REQUIRE_EQ(loc.line, 2u);
	REQUIRE_EQ(loc.column, 3u);
}

TEST_CASE(diagnostics_format_with_source_location)
{
	rexc::SourceFile source("test.rx", "fn main()\n  return 1;\n");
	rexc::Diagnostics diagnostics;

	diagnostics.error(source.location_at(12), "expected expression");

	REQUIRE(diagnostics.has_errors());
	REQUIRE_EQ(diagnostics.format(), std::string("test.rx:2:3: error: expected expression\n"));
}

TEST_CASE(diagnostics_format_caps_human_output_at_eight_lines)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;

	for (int i = 0; i < 10; ++i)
		diagnostics.error(source.location_at(0), "error " + std::to_string(i));

	std::string formatted = diagnostics.format();
	REQUIRE_EQ(formatted,
	           std::string(
	               "test.rx:1:1: error: error 0\n"
	               "test.rx:1:1: error: error 1\n"
	               "test.rx:1:1: error: error 2\n"
	               "test.rx:1:1: error: error 3\n"
	               "test.rx:1:1: error: error 4\n"
	               "test.rx:1:1: error: error 5\n"
	               "test.rx:1:1: error: error 6\n"
	               "error: too many diagnostics, omitted 3 more\n"));
}

TEST_CASE(diagnostics_json_includes_span_and_fixit_fields)
{
	rexc::SourceFile source("test.rx", "let x: int = 0;\n");
	rexc::Diagnostics diagnostics;
	auto location = source.location_at(7);
	auto replacement = rexc::SourceSpan::from_location(location, 3);

	diagnostics.error(location, "unknown type 'int'",
	                  {rexc::FixIt{"replace with 'i32'", replacement, "i32"}});

	REQUIRE_EQ(diagnostics.format_json(),
	           std::string(
	               "[\n"
	               "  {\n"
	               "    \"severity\": \"error\",\n"
	               "    \"message\": \"unknown type 'int'\",\n"
	               "    \"span\": {\n"
	               "      \"file\": \"test.rx\",\n"
	               "      \"start\": { \"offset\": 7, \"line\": 1, \"column\": 8 },\n"
	               "      \"end\": { \"offset\": 10, \"line\": 1, \"column\": 11 }\n"
	               "    },\n"
	               "    \"fixits\": [\n"
	               "      {\n"
	               "        \"message\": \"replace with 'i32'\",\n"
	               "        \"replacement\": \"i32\",\n"
	               "        \"span\": {\n"
	               "          \"file\": \"test.rx\",\n"
	               "          \"start\": { \"offset\": 7, \"line\": 1, \"column\": 8 },\n"
	               "          \"end\": { \"offset\": 10, \"line\": 1, \"column\": 11 }\n"
	               "        }\n"
	               "      }\n"
	               "    ]\n"
	               "  }\n"
	               "]\n"));
}
