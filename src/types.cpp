#include "rexc/types.hpp"

#include <limits>

namespace rexc {

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

bool is_integer(PrimitiveType type)
{
	return is_signed_integer(type) || is_unsigned_integer(type);
}

bool is_signed_integer(PrimitiveType type)
{
	return type.kind == PrimitiveKind::SignedInteger;
}

bool is_unsigned_integer(PrimitiveType type)
{
	return type.kind == PrimitiveKind::UnsignedInteger;
}

bool is_i386_codegen_supported(PrimitiveType type)
{
	return !(is_integer(type) && type.bits == 64);
}

bool integer_literal_fits(PrimitiveType type, std::int64_t value)
{
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

	return false;
}

} // namespace rexc
