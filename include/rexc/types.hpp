#pragma once

// Shared type model used by sema, IR, and codegen.
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace rexc {

enum class PrimitiveKind {
	SignedInteger,
	UnsignedInteger,
	Bool,
	Char,
	Str,
	Pointer,
	OwnedStr,
	Slice,
	Vector,
	Result,
};

struct PrimitiveType {
	PrimitiveKind kind;
	int bits = 0;
	std::shared_ptr<const PrimitiveType> pointee;

	friend bool operator==(const PrimitiveType &lhs, const PrimitiveType &rhs)
	{
		if (lhs.kind != rhs.kind || lhs.bits != rhs.bits)
			return false;
		if (lhs.kind != PrimitiveKind::Pointer && lhs.kind != PrimitiveKind::Slice &&
		    lhs.kind != PrimitiveKind::Vector && lhs.kind != PrimitiveKind::Result)
			return true;
		if (!lhs.pointee || !rhs.pointee)
			return lhs.pointee == rhs.pointee;
		return *lhs.pointee == *rhs.pointee;
	}

	friend bool operator!=(const PrimitiveType &lhs, const PrimitiveType &rhs)
	{
		return !(lhs == rhs);
	}
};

PrimitiveType pointer_to(PrimitiveType pointee);
PrimitiveType slice_of(PrimitiveType element);
PrimitiveType vector_of(PrimitiveType element);
PrimitiveType result_of(PrimitiveType value);
std::optional<PrimitiveType> parse_primitive_type(const std::string &name);
std::string format_type(PrimitiveType type);
bool is_valid_primitive_type(PrimitiveType type);
bool is_integer(PrimitiveType type);
bool is_signed_integer(PrimitiveType type);
bool is_unsigned_integer(PrimitiveType type);
bool is_pointer(PrimitiveType type);
bool is_owned_str(PrimitiveType type);
bool is_slice(PrimitiveType type);
bool is_vector(PrimitiveType type);
bool is_result(PrimitiveType type);
bool is_handle(PrimitiveType type);
std::optional<PrimitiveType> pointee_type(PrimitiveType type);
std::optional<PrimitiveType> handle_payload_type(PrimitiveType type);
bool is_i386_codegen_supported(PrimitiveType type);
bool integer_literal_fits(PrimitiveType type, std::int64_t value);
bool unsigned_integer_literal_fits(PrimitiveType type, std::uint64_t value);

} // namespace rexc
