#include "rexc/types.hpp"
#include "test_support.hpp"

#include <string>

TEST_CASE(types_parse_all_core_primitive_names)
{
	const char *names[] = {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "bool", "char", "str"};
	for (const char *name : names) {
		auto type = rexc::parse_primitive_type(name);
		REQUIRE(type.has_value());
		REQUIRE_EQ(rexc::format_type(*type), std::string(name));
	}
}

TEST_CASE(types_report_integer_properties)
{
	auto i16 = *rexc::parse_primitive_type("i16");
	auto u32 = *rexc::parse_primitive_type("u32");
	auto str = *rexc::parse_primitive_type("str");

	REQUIRE(rexc::is_integer(i16));
	REQUIRE(rexc::is_signed_integer(i16));
	REQUIRE(rexc::is_integer(u32));
	REQUIRE(!rexc::is_signed_integer(u32));
	REQUIRE(!rexc::is_integer(str));
	REQUIRE(rexc::is_i386_codegen_supported(i16));
	REQUIRE(rexc::is_i386_codegen_supported(u32));
	REQUIRE(!rexc::is_i386_codegen_supported(*rexc::parse_primitive_type("i64")));
	REQUIRE(!rexc::is_i386_codegen_supported(*rexc::parse_primitive_type("u64")));
}

TEST_CASE(types_check_integer_literal_ranges)
{
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i8"), -128));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i8"), 127));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("i8"), 128));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("u8"), 255));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("u8"), -1));
}
