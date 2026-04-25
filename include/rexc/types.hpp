#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rexc {

enum class PrimitiveKind {
	SignedInteger,
	UnsignedInteger,
	Bool,
	Char,
	Str,
};

struct PrimitiveType {
	PrimitiveKind kind;
	int bits = 0;

	friend bool operator==(PrimitiveType lhs, PrimitiveType rhs)
	{
		return lhs.kind == rhs.kind && lhs.bits == rhs.bits;
	}

	friend bool operator!=(PrimitiveType lhs, PrimitiveType rhs)
	{
		return !(lhs == rhs);
	}
};

std::optional<PrimitiveType> parse_primitive_type(const std::string &name);
std::string format_type(PrimitiveType type);
bool is_valid_primitive_type(PrimitiveType type);
bool is_integer(PrimitiveType type);
bool is_signed_integer(PrimitiveType type);
bool is_unsigned_integer(PrimitiveType type);
bool is_i386_codegen_supported(PrimitiveType type);
bool integer_literal_fits(PrimitiveType type, std::int64_t value);
bool unsigned_integer_literal_fits(PrimitiveType type, std::uint64_t value);

} // namespace rexc
