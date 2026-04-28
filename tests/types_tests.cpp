// Primitive type model tests shared by sema, IR, and codegen.
//
// These tests pin the shared PrimitiveType rules: spelling, formatting,
// signedness, backend support, and literal bounds. Keeping this model tested
// prevents stages from drifting apart on type behavior.
#include "rexc/types.hpp"
#include "test_support.hpp"

#include <cstdint>
#include <limits>
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

TEST_CASE(types_parse_and_format_pointer_types)
{
	auto pointer = rexc::parse_primitive_type("*i32");
	auto nested = rexc::parse_primitive_type("**u8");

	REQUIRE(pointer.has_value());
	REQUIRE(nested.has_value());
	REQUIRE(rexc::is_pointer(*pointer));
	REQUIRE(rexc::is_pointer(*nested));
	REQUIRE_EQ(rexc::format_type(*pointer), std::string("*i32"));
	REQUIRE_EQ(rexc::format_type(*nested), std::string("**u8"));
	REQUIRE_EQ(rexc::format_type(*rexc::pointee_type(*pointer)), std::string("i32"));
	REQUIRE_EQ(rexc::format_type(*rexc::pointee_type(*nested)), std::string("*u8"));
	REQUIRE(rexc::is_i386_codegen_supported(*pointer));
	REQUIRE(rexc::is_i386_codegen_supported(*rexc::parse_primitive_type("*i64")));
}

TEST_CASE(types_parse_and_format_handle_types)
{
	auto owned = rexc::parse_primitive_type("owned_str");
	auto slice = rexc::parse_primitive_type("slice<i32>");
	auto vec = rexc::parse_primitive_type("vec<i32>");
	auto result = rexc::parse_primitive_type("Result<i32>");

	REQUIRE(owned.has_value());
	REQUIRE(slice.has_value());
	REQUIRE(vec.has_value());
	REQUIRE(result.has_value());
	REQUIRE_EQ(rexc::format_type(*owned), std::string("owned_str"));
	REQUIRE_EQ(rexc::format_type(*slice), std::string("slice<i32>"));
	REQUIRE_EQ(rexc::format_type(*vec), std::string("vec<i32>"));
	REQUIRE_EQ(rexc::format_type(*result), std::string("Result<i32>"));
	REQUIRE(rexc::is_i386_codegen_supported(*owned));
	REQUIRE(rexc::is_i386_codegen_supported(*slice));
	REQUIRE(rexc::is_i386_codegen_supported(*vec));
	REQUIRE(rexc::is_i386_codegen_supported(*result));
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
	REQUIRE(rexc::is_unsigned_integer(u32));
	REQUIRE(!rexc::is_unsigned_integer(i16));
	REQUIRE(!rexc::is_unsigned_integer(str));
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
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i16"), std::numeric_limits<std::int16_t>::min()));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i16"), std::numeric_limits<std::int16_t>::max()));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("i16"), static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::max()) + 1));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("u16"), std::numeric_limits<std::uint16_t>::max()));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("u16"), -1));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("u16"), static_cast<std::int64_t>(std::numeric_limits<std::uint16_t>::max()) + 1));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i32"), std::numeric_limits<std::int32_t>::min()));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i32"), std::numeric_limits<std::int32_t>::max()));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("i32"), static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("u32"), std::numeric_limits<std::uint32_t>::max()));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("u32"), -1));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("u32"), static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) + 1));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i64"), std::numeric_limits<std::int64_t>::min()));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i64"), std::numeric_limits<std::int64_t>::max()));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("u64"), std::numeric_limits<std::int64_t>::max()));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("u64"), -1));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("bool"), 0));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("char"), 0));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("str"), 0));
}

TEST_CASE(types_check_unsigned_integer_literal_ranges)
{
	REQUIRE(rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("u8"), std::numeric_limits<std::uint8_t>::max()));
	REQUIRE(!rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("u8"), static_cast<std::uint64_t>(std::numeric_limits<std::uint8_t>::max()) + 1));
	REQUIRE(rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("u16"), std::numeric_limits<std::uint16_t>::max()));
	REQUIRE(!rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("u16"), static_cast<std::uint64_t>(std::numeric_limits<std::uint16_t>::max()) + 1));
	REQUIRE(rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("u32"), std::numeric_limits<std::uint32_t>::max()));
	REQUIRE(!rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("u32"), static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1));
	REQUIRE(rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("u64"), std::numeric_limits<std::uint64_t>::max()));
	REQUIRE(!rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("i64"), 0));
	REQUIRE(!rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("bool"), 0));
	REQUIRE(!rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("char"), 0));
	REQUIRE(!rexc::unsigned_integer_literal_fits(*rexc::parse_primitive_type("str"), 0));
}

TEST_CASE(types_reject_invalid_primitive_shapes)
{
	rexc::PrimitiveType signed_128{rexc::PrimitiveKind::SignedInteger, 128};
	rexc::PrimitiveType unsigned_128{rexc::PrimitiveKind::UnsignedInteger, 128};
	rexc::PrimitiveType bool_with_bits{rexc::PrimitiveKind::Bool, 1};

	REQUIRE(!rexc::is_valid_primitive_type(signed_128));
	REQUIRE(!rexc::is_valid_primitive_type(unsigned_128));
	REQUIRE(!rexc::is_valid_primitive_type(bool_with_bits));
	REQUIRE(!rexc::is_integer(signed_128));
	REQUIRE(!rexc::is_signed_integer(signed_128));
	REQUIRE(!rexc::is_unsigned_integer(unsigned_128));
	REQUIRE_EQ(rexc::format_type(signed_128), std::string(""));
	REQUIRE_EQ(rexc::format_type(bool_with_bits), std::string(""));
	REQUIRE(!rexc::is_i386_codegen_supported(signed_128));
	REQUIRE(!rexc::is_i386_codegen_supported(bool_with_bits));
	REQUIRE(!rexc::integer_literal_fits(signed_128, 0));
	REQUIRE(!rexc::integer_literal_fits(bool_with_bits, 0));
	REQUIRE(!rexc::unsigned_integer_literal_fits(unsigned_128, 0));
}
