// Unit coverage for source locations and diagnostic formatting.
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
