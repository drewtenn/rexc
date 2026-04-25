// Primitive type utilities shared across compiler stages.
//
// The parser preserves type spelling, sema resolves that spelling with these
// helpers, IR stores the resolved PrimitiveType, and codegen asks the same
// model about widths, signedness, formatting, literal ranges, and target
// support. Keeping this here prevents each stage from inventing its own type
// rules.
#include "rexc/types.hpp"

#include <limits>

namespace rexc {

namespace {

bool is_valid_integer_width(int bits)
{
	return bits == 8 || bits == 16 || bits == 32 || bits == 64;
}

} // namespace

std::optional<PrimitiveType> parse_primitive_type(const std::string &name)
{
	if (name == "i8")
		return PrimitiveType{PrimitiveKind::SignedInteger, 8};
	if (name == "i16")
		return PrimitiveType{PrimitiveKind::SignedInteger, 16};
	if (name == "i32")
		return PrimitiveType{PrimitiveKind::SignedInteger, 32};
	if (name == "i64")
		return PrimitiveType{PrimitiveKind::SignedInteger, 64};
	if (name == "u8")
		return PrimitiveType{PrimitiveKind::UnsignedInteger, 8};
	if (name == "u16")
		return PrimitiveType{PrimitiveKind::UnsignedInteger, 16};
	if (name == "u32")
		return PrimitiveType{PrimitiveKind::UnsignedInteger, 32};
	if (name == "u64")
		return PrimitiveType{PrimitiveKind::UnsignedInteger, 64};
	if (name == "bool")
		return PrimitiveType{PrimitiveKind::Bool};
	if (name == "char")
		return PrimitiveType{PrimitiveKind::Char};
	if (name == "str")
		return PrimitiveType{PrimitiveKind::Str};
	return std::nullopt;
}

std::string format_type(PrimitiveType type)
{
	if (!is_valid_primitive_type(type))
		return "";

	switch (type.kind) {
	case PrimitiveKind::SignedInteger:
		return "i" + std::to_string(type.bits);
	case PrimitiveKind::UnsignedInteger:
		return "u" + std::to_string(type.bits);
	case PrimitiveKind::Bool:
		return "bool";
	case PrimitiveKind::Char:
		return "char";
	case PrimitiveKind::Str:
		return "str";
	}
	return "";
}

bool is_valid_primitive_type(PrimitiveType type)
{
	switch (type.kind) {
	case PrimitiveKind::SignedInteger:
	case PrimitiveKind::UnsignedInteger:
		return is_valid_integer_width(type.bits);
	case PrimitiveKind::Bool:
	case PrimitiveKind::Char:
	case PrimitiveKind::Str:
		return type.bits == 0;
	}
	return false;
}

bool is_integer(PrimitiveType type)
{
	return is_signed_integer(type) || is_unsigned_integer(type);
}

bool is_signed_integer(PrimitiveType type)
{
	return is_valid_primitive_type(type) && type.kind == PrimitiveKind::SignedInteger;
}

bool is_unsigned_integer(PrimitiveType type)
{
	return is_valid_primitive_type(type) && type.kind == PrimitiveKind::UnsignedInteger;
}

bool is_i386_codegen_supported(PrimitiveType type)
{
	if (!is_valid_primitive_type(type))
		return false;
	return !(is_integer(type) && type.bits == 64);
}

bool integer_literal_fits(PrimitiveType type, std::int64_t value)
{
	if (!is_valid_primitive_type(type))
		return false;

	if (is_signed_integer(type)) {
		switch (type.bits) {
		case 8:
			return value >= std::numeric_limits<std::int8_t>::min() && value <= std::numeric_limits<std::int8_t>::max();
		case 16:
			return value >= std::numeric_limits<std::int16_t>::min() && value <= std::numeric_limits<std::int16_t>::max();
		case 32:
			return value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max();
		case 64:
			return true;
		default:
			return false;
		}
	}

	if (is_unsigned_integer(type)) {
		if (value < 0)
			return false;
		return unsigned_integer_literal_fits(type, static_cast<std::uint64_t>(value));
	}

	return false;
}

bool unsigned_integer_literal_fits(PrimitiveType type, std::uint64_t value)
{
	if (!is_unsigned_integer(type))
		return false;

	switch (type.bits) {
	case 8:
		return value <= std::numeric_limits<std::uint8_t>::max();
	case 16:
		return value <= std::numeric_limits<std::uint16_t>::max();
	case 32:
		return value <= std::numeric_limits<std::uint32_t>::max();
	case 64:
		return true;
	default:
		return false;
	}
}

} // namespace rexc
