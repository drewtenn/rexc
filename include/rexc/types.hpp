#pragma once

// Shared type model used by sema, IR, and codegen.
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
	Option,
	Result,
	Tuple,
	UserStruct,
	UserEnum,
};

struct PrimitiveType {
	PrimitiveKind kind;
	int bits = 0;
	std::shared_ptr<const PrimitiveType> pointee;
	std::shared_ptr<const std::vector<PrimitiveType>> elements;
	std::string name; // populated for UserStruct and UserEnum kinds

	friend bool operator==(const PrimitiveType &lhs, const PrimitiveType &rhs)
	{
		if (lhs.kind != rhs.kind || lhs.bits != rhs.bits)
			return false;
		if (lhs.kind == PrimitiveKind::UserStruct || lhs.kind == PrimitiveKind::UserEnum)
			return lhs.name == rhs.name;
		if (lhs.kind == PrimitiveKind::Tuple) {
			if (!lhs.elements || !rhs.elements)
				return lhs.elements == rhs.elements;
			return *lhs.elements == *rhs.elements;
		}
		if (lhs.kind == PrimitiveKind::Result) {
			if (lhs.elements || rhs.elements) {
				if (!lhs.elements || !rhs.elements)
					return false;
				return *lhs.elements == *rhs.elements;
			}
		}
		if (lhs.kind != PrimitiveKind::Pointer && lhs.kind != PrimitiveKind::Slice &&
		    lhs.kind != PrimitiveKind::Vector && lhs.kind != PrimitiveKind::Option &&
		    lhs.kind != PrimitiveKind::Result)
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

struct EnumVariantSpec {
	std::string name;
	std::vector<PrimitiveType> payload_types;
};

struct EnumPayloadFieldLayout {
	PrimitiveType type;
	std::size_t offset = 0;
};

struct EnumVariantLayout {
	std::string name;
	std::uint32_t tag = 0;
	std::vector<PrimitiveType> payload_types;
	std::vector<EnumPayloadFieldLayout> fields;
	std::size_t payload_size = 0;
	std::size_t payload_alignment = 1;
};

struct EnumLayout {
	// Rexy enum values are a tagged union: a fixed u32 tag at byte 0 followed
	// by storage large enough for the largest variant payload. The payload
	// starts at the first offset after the tag that satisfies the maximum
	// payload alignment.
	std::size_t tag_size = 4;
	std::size_t tag_alignment = 4;
	std::size_t payload_offset = 4;
	std::size_t total_size = 4;
	std::size_t alignment = 4;
	std::vector<EnumVariantLayout> variants;
};

struct TupleElementLayout {
	PrimitiveType type;
	std::size_t offset = 0;
};

struct TupleLayout {
	std::vector<TupleElementLayout> elements;
	std::size_t total_size = 0;
	std::size_t alignment = 1;
};

PrimitiveType pointer_to(PrimitiveType pointee);
PrimitiveType slice_of(PrimitiveType element);
PrimitiveType vector_of(PrimitiveType element);
PrimitiveType option_of(PrimitiveType value);
PrimitiveType result_of(PrimitiveType value);
PrimitiveType result_of(PrimitiveType value, PrimitiveType error);
PrimitiveType tuple_type(std::vector<PrimitiveType> elements);
PrimitiveType user_struct_type(std::string name, int bits = 0);
PrimitiveType user_enum_type(std::string name, int bits = 0);
bool is_user_struct(PrimitiveType type);
bool is_user_enum(PrimitiveType type);
bool is_tuple(PrimitiveType type);
std::optional<PrimitiveType> parse_primitive_type(const std::string &name);
std::optional<std::vector<std::string>> split_tuple_type_name(const std::string &name);
std::string format_type(PrimitiveType type);
bool is_valid_primitive_type(PrimitiveType type);
bool is_integer(PrimitiveType type);
bool is_signed_integer(PrimitiveType type);
bool is_unsigned_integer(PrimitiveType type);
bool is_pointer(PrimitiveType type);
bool is_owned_str(PrimitiveType type);
bool is_slice(PrimitiveType type);
bool is_vector(PrimitiveType type);
bool is_option(PrimitiveType type);
bool is_result(PrimitiveType type);
bool is_handle(PrimitiveType type);
std::optional<PrimitiveType> pointee_type(PrimitiveType type);
std::optional<PrimitiveType> handle_payload_type(PrimitiveType type);
std::optional<PrimitiveType> result_error_type(PrimitiveType type);
std::optional<std::vector<PrimitiveType>> tuple_elements(PrimitiveType type);
std::optional<std::size_t> type_size_bytes(PrimitiveType type);
std::optional<std::size_t> type_alignment_bytes(PrimitiveType type);
EnumLayout layout_enum_variants(const std::vector<EnumVariantSpec> &variants);
TupleLayout layout_tuple_elements(const std::vector<PrimitiveType> &elements);
bool is_i386_codegen_supported(PrimitiveType type);
bool integer_literal_fits(PrimitiveType type, std::int64_t value);
bool unsigned_integer_literal_fits(PrimitiveType type, std::uint64_t value);

// FE-103: unify a generic-parameterized PATTERN against a concrete ACTUAL
// type, recording bindings as it walks. `generic_names` is the set of
// names treated as type variables (e.g. {"T", "U"}). Returns true on
// successful unification.
bool unify_generic_pattern(PrimitiveType pattern, PrimitiveType actual,
                           const std::unordered_set<std::string> &generic_names,
                           std::unordered_map<std::string, PrimitiveType> &bindings);

// FE-103: substitute generic parameter occurrences in `type` using
// `bindings`. Names not in `bindings` are left unchanged.
PrimitiveType substitute_generics(
    PrimitiveType type,
    const std::unordered_map<std::string, PrimitiveType> &bindings);

// FE-103: mangle an instantiation suffix from a binding map, e.g.
// {T -> i32, U -> bool} -> "__i32_bool". Order follows `parameter_order`.
std::string mangle_generic_suffix(
    const std::vector<std::string> &parameter_order,
    const std::unordered_map<std::string, PrimitiveType> &bindings);

} // namespace rexc
