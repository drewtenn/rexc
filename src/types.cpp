// Type utilities shared across compiler stages.
//
// The parser preserves type spelling, sema resolves that spelling with these
// helpers, IR stores the resolved PrimitiveType, and codegen asks the same
// model about widths, signedness, pointer pointees, formatting, literal ranges,
// and target support. Keeping this here prevents each stage from inventing its
// own type rules.
#include "rexc/types.hpp"

#include <cstddef>
#include <limits>
#include <utility>

namespace rexc {

namespace {

bool is_valid_integer_width(int bits)
{
	return bits == 8 || bits == 16 || bits == 32 || bits == 64;
}

bool consume_generic(const std::string &name, const std::string &prefix,
                     std::string &inner)
{
	const std::string open = prefix + "<";
	if (name.size() <= open.size() || name.rfind(open, 0) != 0 || name.back() != '>')
		return false;

	int depth = 0;
	for (std::size_t i = prefix.size(); i < name.size(); ++i) {
		if (name[i] == '<')
			++depth;
		else if (name[i] == '>') {
			--depth;
			if (depth == 0 && i != name.size() - 1)
				return false;
			if (depth < 0)
				return false;
		}
	}
	if (depth != 0)
		return false;

	inner = name.substr(prefix.size() + 1, name.size() - prefix.size() - 2);
	return !inner.empty();
}

PrimitiveType handle_with_payload(PrimitiveKind kind, PrimitiveType payload)
{
	return PrimitiveType{kind, 0,
	                     std::make_shared<const PrimitiveType>(std::move(payload))};
}

} // namespace

PrimitiveType pointer_to(PrimitiveType pointee)
{
	return PrimitiveType{PrimitiveKind::Pointer, 0,
	                     std::make_shared<const PrimitiveType>(std::move(pointee))};
}

PrimitiveType slice_of(PrimitiveType element)
{
	return handle_with_payload(PrimitiveKind::Slice, std::move(element));
}

PrimitiveType vector_of(PrimitiveType element)
{
	return handle_with_payload(PrimitiveKind::Vector, std::move(element));
}

PrimitiveType result_of(PrimitiveType value)
{
	return handle_with_payload(PrimitiveKind::Result, std::move(value));
}

std::optional<PrimitiveType> parse_primitive_type(const std::string &name)
{
	if (!name.empty() && name.front() == '*') {
		auto pointee = parse_primitive_type(name.substr(1));
		if (!pointee)
			return std::nullopt;
		return pointer_to(*pointee);
	}
	if (name == "owned_str")
		return PrimitiveType{PrimitiveKind::OwnedStr};
	std::string inner;
	if (consume_generic(name, "slice", inner)) {
		auto element = parse_primitive_type(inner);
		if (!element)
			return std::nullopt;
		return slice_of(*element);
	}
	if (consume_generic(name, "vec", inner)) {
		auto element = parse_primitive_type(inner);
		if (!element)
			return std::nullopt;
		return vector_of(*element);
	}
	if (consume_generic(name, "Result", inner)) {
		auto value = parse_primitive_type(inner);
		if (!value)
			return std::nullopt;
		return result_of(*value);
	}
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
	case PrimitiveKind::Pointer:
		return "*" + format_type(*type.pointee);
	case PrimitiveKind::OwnedStr:
		return "owned_str";
	case PrimitiveKind::Slice:
		return "slice<" + format_type(*type.pointee) + ">";
	case PrimitiveKind::Vector:
		return "vec<" + format_type(*type.pointee) + ">";
	case PrimitiveKind::Result:
		return "Result<" + format_type(*type.pointee) + ">";
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
	case PrimitiveKind::OwnedStr:
		return type.bits == 0;
	case PrimitiveKind::Pointer:
	case PrimitiveKind::Slice:
	case PrimitiveKind::Vector:
	case PrimitiveKind::Result:
		return type.bits == 0 && type.pointee != nullptr &&
		       is_valid_primitive_type(*type.pointee);
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

bool is_pointer(PrimitiveType type)
{
	return is_valid_primitive_type(type) && type.kind == PrimitiveKind::Pointer;
}

bool is_owned_str(PrimitiveType type)
{
	return is_valid_primitive_type(type) && type.kind == PrimitiveKind::OwnedStr;
}

bool is_slice(PrimitiveType type)
{
	return is_valid_primitive_type(type) && type.kind == PrimitiveKind::Slice;
}

bool is_vector(PrimitiveType type)
{
	return is_valid_primitive_type(type) && type.kind == PrimitiveKind::Vector;
}

bool is_result(PrimitiveType type)
{
	return is_valid_primitive_type(type) && type.kind == PrimitiveKind::Result;
}

bool is_handle(PrimitiveType type)
{
	return is_owned_str(type) || is_slice(type) || is_vector(type) || is_result(type);
}

std::optional<PrimitiveType> pointee_type(PrimitiveType type)
{
	if (!is_pointer(type))
		return std::nullopt;
	return *type.pointee;
}

std::optional<PrimitiveType> handle_payload_type(PrimitiveType type)
{
	if (!is_slice(type) && !is_vector(type) && !is_result(type))
		return std::nullopt;
	return *type.pointee;
}

bool is_i386_codegen_supported(PrimitiveType type)
{
	if (!is_valid_primitive_type(type))
		return false;
	if (is_pointer(type) || is_handle(type))
		return true;
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
