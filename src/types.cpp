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
#include <algorithm>
#include <cctype>
#include <utility>

namespace rexc {

namespace {

bool is_valid_integer_width(int bits)
{
	return bits == 8 || bits == 16 || bits == 32 || bits == 64;
}

std::string trim(std::string value)
{
	auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
		return std::isspace(ch) != 0;
	});
	auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
		return std::isspace(ch) != 0;
	}).base();
	if (begin >= end)
		return "";
	return std::string(begin, end);
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

std::vector<std::string> split_comma_list(const std::string &text)
{
	std::vector<std::string> parts;
	int paren_depth = 0;
	int angle_depth = 0;
	std::size_t start = 0;
	for (std::size_t i = 0; i < text.size(); ++i) {
		char ch = text[i];
		if (ch == '(')
			++paren_depth;
		else if (ch == ')')
			--paren_depth;
		else if (ch == '<')
			++angle_depth;
		else if (ch == '>')
			--angle_depth;
		else if (ch == ',' && paren_depth == 0 && angle_depth == 0) {
			parts.push_back(trim(text.substr(start, i - start)));
			start = i + 1;
		}
	}
	parts.push_back(trim(text.substr(start)));
	return parts;
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

PrimitiveType option_of(PrimitiveType value)
{
	return handle_with_payload(PrimitiveKind::Option, std::move(value));
}

PrimitiveType result_of(PrimitiveType value)
{
	return handle_with_payload(PrimitiveKind::Result, std::move(value));
}

PrimitiveType result_of(PrimitiveType value, PrimitiveType error)
{
	std::vector<PrimitiveType> elements{value, std::move(error)};
	PrimitiveType type = handle_with_payload(PrimitiveKind::Result, std::move(value));
	type.elements = std::make_shared<const std::vector<PrimitiveType>>(std::move(elements));
	return type;
}

PrimitiveType tuple_type(std::vector<PrimitiveType> elements)
{
	TupleLayout layout = layout_tuple_elements(elements);
	PrimitiveType type;
	type.kind = PrimitiveKind::Tuple;
	type.bits = static_cast<int>(layout.total_size * 8);
	type.elements = std::make_shared<const std::vector<PrimitiveType>>(std::move(elements));
	return type;
}

PrimitiveType user_struct_type(std::string name, int bits)
{
	PrimitiveType type;
	type.kind = PrimitiveKind::UserStruct;
	type.bits = bits;
	type.name = std::move(name);
	return type;
}

PrimitiveType user_enum_type(std::string name, int bits)
{
	PrimitiveType type;
	type.kind = PrimitiveKind::UserEnum;
	type.bits = bits;
	type.name = std::move(name);
	return type;
}

bool is_user_struct(PrimitiveType type)
{
	return type.kind == PrimitiveKind::UserStruct;
}

bool is_user_enum(PrimitiveType type)
{
	return type.kind == PrimitiveKind::UserEnum;
}

bool is_tuple(PrimitiveType type)
{
	return type.kind == PrimitiveKind::Tuple;
}

std::optional<std::vector<std::string>> split_tuple_type_name(const std::string &name)
{
	if (name.size() < 5 || name.front() != '(' || name.back() != ')')
		return std::nullopt;

	std::string inner = name.substr(1, name.size() - 2);
	auto parts = split_comma_list(inner);
	if (!parts.empty() && parts.back().empty())
		parts.pop_back();
	if (parts.size() < 2)
		return std::nullopt;
	for (const auto &part : parts) {
		if (part.empty())
			return std::nullopt;
	}
	return parts;
}

std::optional<PrimitiveType> parse_primitive_type(const std::string &name)
{
	if (!name.empty() && name.front() == '*') {
		auto pointee = parse_primitive_type(name.substr(1));
		if (!pointee)
			return std::nullopt;
		return pointer_to(*pointee);
	}
	if (name.size() > 3 && name.rfind("&[", 0) == 0 && name.back() == ']') {
		auto element = parse_primitive_type(name.substr(2, name.size() - 3));
		if (!element)
			return std::nullopt;
		return slice_of(*element);
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
	if (consume_generic(name, "Option", inner)) {
		auto value = parse_primitive_type(inner);
		if (!value)
			return std::nullopt;
		return option_of(*value);
	}
	if (consume_generic(name, "Result", inner)) {
		auto parts = split_comma_list(inner);
		if (parts.size() != 1 && parts.size() != 2)
			return std::nullopt;
		auto value = parse_primitive_type(parts[0]);
		if (!value)
			return std::nullopt;
		if (parts.size() == 1)
			return result_of(*value);
		auto error = parse_primitive_type(parts[1]);
		if (!error)
			return std::nullopt;
		return result_of(*value, *error);
	}
	if (auto tuple_names = split_tuple_type_name(name)) {
		std::vector<PrimitiveType> elements;
		elements.reserve(tuple_names->size());
		for (const auto &element_name : *tuple_names) {
			auto element = parse_primitive_type(element_name);
			if (!element)
				return std::nullopt;
			elements.push_back(*element);
		}
		return tuple_type(std::move(elements));
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
		return "&[" + format_type(*type.pointee) + "]";
	case PrimitiveKind::Vector:
		return "vec<" + format_type(*type.pointee) + ">";
	case PrimitiveKind::Option:
		return "Option<" + format_type(*type.pointee) + ">";
	case PrimitiveKind::Result:
		if (type.elements && type.elements->size() == 2) {
			return "Result<" + format_type((*type.elements)[0]) + ", " +
			       format_type((*type.elements)[1]) + ">";
		}
		return "Result<" + format_type(*type.pointee) + ">";
	case PrimitiveKind::Tuple: {
		if (!type.elements)
			return "";
		std::string formatted = "(";
		for (std::size_t i = 0; i < type.elements->size(); ++i) {
			if (i > 0)
				formatted += ", ";
			formatted += format_type((*type.elements)[i]);
		}
		formatted += ")";
		return formatted;
	}
	case PrimitiveKind::UserStruct:
	case PrimitiveKind::UserEnum:
		return type.name;
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
	case PrimitiveKind::Option:
	case PrimitiveKind::Result:
		if (type.bits != 0 || type.pointee == nullptr ||
		    !is_valid_primitive_type(*type.pointee))
			return false;
		if (type.kind == PrimitiveKind::Result && type.elements) {
			if (type.elements->size() != 2)
				return false;
			if ((*type.elements)[0] != *type.pointee)
				return false;
			for (const auto &element : *type.elements) {
				if (!is_valid_primitive_type(element))
					return false;
			}
		}
		return true;
	case PrimitiveKind::Tuple:
		if (!type.elements || type.elements->size() < 2 || type.bits <= 0)
			return false;
		for (const auto &element : *type.elements) {
			if (!is_valid_primitive_type(element))
				return false;
		}
		return true;
	case PrimitiveKind::UserStruct:
	case PrimitiveKind::UserEnum:
		return type.bits >= 0 && !type.name.empty();
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

bool is_option(PrimitiveType type)
{
	return is_valid_primitive_type(type) && type.kind == PrimitiveKind::Option;
}

bool is_result(PrimitiveType type)
{
	return is_valid_primitive_type(type) && type.kind == PrimitiveKind::Result;
}

bool is_handle(PrimitiveType type)
{
	return is_owned_str(type) || is_slice(type) || is_vector(type) || is_option(type) ||
	       is_result(type);
}

std::optional<PrimitiveType> pointee_type(PrimitiveType type)
{
	if (!is_pointer(type))
		return std::nullopt;
	return *type.pointee;
}

std::optional<PrimitiveType> handle_payload_type(PrimitiveType type)
{
	if (!is_slice(type) && !is_vector(type) && !is_option(type) && !is_result(type))
		return std::nullopt;
	return *type.pointee;
}

std::optional<PrimitiveType> result_error_type(PrimitiveType type)
{
	if (!is_result(type) || !type.elements || type.elements->size() != 2)
		return std::nullopt;
	return (*type.elements)[1];
}

std::optional<std::vector<PrimitiveType>> tuple_elements(PrimitiveType type)
{
	if (!is_tuple(type) || !type.elements)
		return std::nullopt;
	return *type.elements;
}

std::optional<std::size_t> type_size_bytes(PrimitiveType type)
{
	if (!is_valid_primitive_type(type))
		return std::nullopt;

	switch (type.kind) {
	case PrimitiveKind::SignedInteger:
	case PrimitiveKind::UnsignedInteger:
		return static_cast<std::size_t>(type.bits) / 8u;
	case PrimitiveKind::Bool:
		return 1u;
	case PrimitiveKind::Char:
		return 4u;
	case PrimitiveKind::Pointer:
	case PrimitiveKind::Str:
	case PrimitiveKind::OwnedStr:
	case PrimitiveKind::Slice:
	case PrimitiveKind::Vector:
	case PrimitiveKind::Option:
	case PrimitiveKind::Result:
		return 8u;
	case PrimitiveKind::Tuple:
		if (type.bits <= 0)
			return std::nullopt;
		return static_cast<std::size_t>(type.bits) / 8u;
	case PrimitiveKind::UserStruct:
	case PrimitiveKind::UserEnum:
		if (type.bits <= 0)
			return std::nullopt;
		return static_cast<std::size_t>(type.bits) / 8u;
	}
	return std::nullopt;
}

std::optional<std::size_t> type_alignment_bytes(PrimitiveType type)
{
	if (is_tuple(type) && type.elements)
		return layout_tuple_elements(*type.elements).alignment;

	auto size = type_size_bytes(type);
	if (!size)
		return std::nullopt;
	return std::max<std::size_t>(*size, 1u);
}

namespace {

std::size_t align_up(std::size_t value, std::size_t alignment)
{
	if (alignment <= 1)
		return value;
	return (value + alignment - 1) / alignment * alignment;
}

} // namespace

EnumLayout layout_enum_variants(const std::vector<EnumVariantSpec> &variants)
{
	EnumLayout layout;
	layout.variants.reserve(variants.size());

	std::size_t max_payload_size = 0;
	std::size_t max_payload_alignment = 1;
	for (std::size_t i = 0; i < variants.size(); ++i) {
		const auto &spec = variants[i];
		EnumVariantLayout variant;
		variant.name = spec.name;
		variant.tag = static_cast<std::uint32_t>(i);
		variant.payload_types = spec.payload_types;

		std::size_t offset = 0;
		for (PrimitiveType payload_type : spec.payload_types) {
			std::size_t field_alignment = type_alignment_bytes(payload_type).value_or(1u);
			std::size_t field_size = type_size_bytes(payload_type).value_or(0u);
			offset = align_up(offset, field_alignment);
			variant.fields.push_back(EnumPayloadFieldLayout{payload_type, offset});
			offset += field_size;
			variant.payload_alignment =
			    std::max(variant.payload_alignment, field_alignment);
		}

		variant.payload_size = align_up(offset, variant.payload_alignment);
		max_payload_size = std::max(max_payload_size, variant.payload_size);
		max_payload_alignment =
		    std::max(max_payload_alignment, variant.payload_alignment);
		layout.variants.push_back(std::move(variant));
	}

	layout.payload_offset = align_up(layout.tag_size, max_payload_alignment);
	layout.alignment = std::max(layout.tag_alignment, max_payload_alignment);
	layout.total_size = align_up(layout.payload_offset + max_payload_size,
	                             layout.alignment);
	return layout;
}

TupleLayout layout_tuple_elements(const std::vector<PrimitiveType> &elements)
{
	TupleLayout layout;
	layout.elements.reserve(elements.size());

	std::size_t offset = 0;
	for (PrimitiveType element_type : elements) {
		std::size_t element_alignment = type_alignment_bytes(element_type).value_or(1u);
		std::size_t element_size = type_size_bytes(element_type).value_or(0u);
		offset = align_up(offset, element_alignment);
		layout.elements.push_back(TupleElementLayout{element_type, offset});
		offset += element_size;
		layout.alignment = std::max(layout.alignment, element_alignment);
	}
	layout.total_size = align_up(offset, layout.alignment);
	return layout;
}

bool is_i386_codegen_supported(PrimitiveType type)
{
	if (!is_valid_primitive_type(type))
		return false;
	if (is_pointer(type) || is_handle(type))
		return true;
	if (is_user_enum(type))
		return true;
	if (is_tuple(type))
		return true;
	if (is_user_struct(type))
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

bool unify_generic_pattern(PrimitiveType pattern, PrimitiveType actual,
                           const std::unordered_set<std::string> &generic_names,
                           std::unordered_map<std::string, PrimitiveType> &bindings)
{
	// FE-103: type variables are modelled as UserStruct sentinels named
	// after the generic parameter (matching FE-102's check_type behaviour).
	if (pattern.kind == PrimitiveKind::UserStruct &&
	    generic_names.count(pattern.name) > 0) {
		auto existing = bindings.find(pattern.name);
		if (existing != bindings.end())
			return existing->second == actual;
		bindings[pattern.name] = actual;
		return true;
	}

	if (pattern.kind != actual.kind)
		return false;

	switch (pattern.kind) {
	case PrimitiveKind::Pointer:
	case PrimitiveKind::Slice:
	case PrimitiveKind::Vector:
	case PrimitiveKind::Option:
		if (!pattern.pointee || !actual.pointee)
			return pattern.pointee == actual.pointee;
		return unify_generic_pattern(*pattern.pointee, *actual.pointee,
		                             generic_names, bindings);
	case PrimitiveKind::Result: {
		if (pattern.elements && actual.elements) {
			if (pattern.elements->size() != actual.elements->size())
				return false;
			for (std::size_t i = 0; i < pattern.elements->size(); ++i) {
				if (!unify_generic_pattern((*pattern.elements)[i],
				                           (*actual.elements)[i],
				                           generic_names, bindings))
					return false;
			}
			return true;
		}
		if (pattern.pointee && actual.pointee)
			return unify_generic_pattern(*pattern.pointee, *actual.pointee,
			                             generic_names, bindings);
		return pattern.pointee == actual.pointee &&
		       pattern.elements == actual.elements;
	}
	case PrimitiveKind::Tuple: {
		if (!pattern.elements || !actual.elements)
			return pattern.elements == actual.elements;
		if (pattern.elements->size() != actual.elements->size())
			return false;
		for (std::size_t i = 0; i < pattern.elements->size(); ++i) {
			if (!unify_generic_pattern((*pattern.elements)[i],
			                           (*actual.elements)[i],
			                           generic_names, bindings))
				return false;
		}
		return true;
	}
	case PrimitiveKind::UserStruct:
	case PrimitiveKind::UserEnum:
		return pattern.name == actual.name;
	default:
		return pattern == actual;
	}
}

namespace {

// FE-103.1: forward declarations for the string-walking helpers used by
// substitute_generics's UserStruct branch. The implementations sit alongside
// other anonymous-namespace helpers below so they can reach
// trim_type_text_internal; the public `consume_generic_type_arguments` they
// also call is forward-declared in types.hpp.
std::string substitute_generic_name(
    const std::string &name,
    const std::unordered_map<std::string, PrimitiveType> &bindings);

std::string substitute_arg_text(
    const std::string &arg_in,
    const std::unordered_map<std::string, PrimitiveType> &bindings);

} // namespace

PrimitiveType substitute_generics(
    PrimitiveType type,
    const std::unordered_map<std::string, PrimitiveType> &bindings)
{
	if (type.kind == PrimitiveKind::UserStruct) {
		auto it = bindings.find(type.name);
		if (it != bindings.end())
			return it->second;
		// FE-103.1: walk string-encoded nested template names like "Box<T>"
		// and substitute generic-param tokens inside the angle-bracket args.
		// Returns angle-bracket form (e.g. "Box<i32>") so callers can
		// re-resolve through check_type_name / lower_type_name to mangle and
		// register the monomorphized layout.
		std::string substituted = substitute_generic_name(type.name, bindings);
		if (substituted != type.name) {
			PrimitiveType result = type;
			result.name = substituted;
			result.bits = 0; // bits are stale post-substitution; consumer re-resolves.
			return result;
		}
		return type;
	}

	auto rebuild_pointee = [&](PrimitiveType t) {
		if (t.pointee)
			t.pointee = std::make_shared<const PrimitiveType>(
			    substitute_generics(*t.pointee, bindings));
		return t;
	};

	switch (type.kind) {
	case PrimitiveKind::Pointer:
	case PrimitiveKind::Slice:
	case PrimitiveKind::Vector:
	case PrimitiveKind::Option:
		return rebuild_pointee(type);
	case PrimitiveKind::Result: {
		PrimitiveType result = type;
		if (result.elements) {
			std::vector<PrimitiveType> elements;
			elements.reserve(result.elements->size());
			for (const auto &e : *result.elements)
				elements.push_back(substitute_generics(e, bindings));
			result.elements =
			    std::make_shared<const std::vector<PrimitiveType>>(std::move(elements));
		} else if (result.pointee) {
			result.pointee = std::make_shared<const PrimitiveType>(
			    substitute_generics(*result.pointee, bindings));
		}
		return result;
	}
	case PrimitiveKind::Tuple: {
		PrimitiveType result = type;
		if (result.elements) {
			std::vector<PrimitiveType> elements;
			elements.reserve(result.elements->size());
			for (const auto &e : *result.elements)
				elements.push_back(substitute_generics(e, bindings));
			result.elements =
			    std::make_shared<const std::vector<PrimitiveType>>(std::move(elements));
		}
		return result;
	}
	default:
		return type;
	}
}

namespace {

std::string trim_type_text_internal(std::string value)
{
	auto begin = std::find_if_not(value.begin(), value.end(),
	                              [](unsigned char ch) { return std::isspace(ch) != 0; });
	auto end = std::find_if_not(value.rbegin(), value.rend(),
	                            [](unsigned char ch) { return std::isspace(ch) != 0; })
	               .base();
	if (begin >= end)
		return "";
	return std::string(begin, end);
}

// FE-103.1: substitute generic-param tokens inside a string-encoded
// template name like "Box<T>", returning the canonical angle-bracket form
// (e.g. "Box<i32>"). Names that don't parse as Foo<...> are returned
// unchanged.
//
// Tuples ("(i32, T)") and slices ("&[T]") inside generic args are NOT
// substituted here; they're a known gap, not exercised by the FE-103
// exit-test surface. Adding them is straightforward but currently
// unnecessary.
std::string substitute_generic_name(
    const std::string &name,
    const std::unordered_map<std::string, PrimitiveType> &bindings)
{
	auto open = name.find('<');
	if (open == std::string::npos || name.empty() || name.back() != '>')
		return name;
	std::string base = name.substr(0, open);
	auto args = consume_generic_type_arguments(name, base);
	if (!args)
		return name;
	std::string result = base + "<";
	for (std::size_t i = 0; i < args->size(); ++i) {
		if (i > 0)
			result += ", ";
		result += substitute_arg_text((*args)[i], bindings);
	}
	result += ">";
	return result;
}

std::string substitute_arg_text(
    const std::string &arg_in,
    const std::unordered_map<std::string, PrimitiveType> &bindings)
{
	std::string arg = trim_type_text_internal(arg_in);
	if (auto it = bindings.find(arg); it != bindings.end())
		return format_type(it->second);
	if (!arg.empty() && arg.front() == '*')
		return "*" + substitute_arg_text(arg.substr(1), bindings);
	if (!arg.empty() && arg.back() == '>' &&
	    arg.find('<') != std::string::npos)
		return substitute_generic_name(arg, bindings);
	// Concrete primitive (i32, bool, ...) or a not-currently-substituted
	// token; leave as-is. Tuples/slices fall through here on purpose.
	return arg;
}

} // namespace

std::vector<std::string> split_type_arguments(const std::string &text)
{
	std::vector<std::string> parts;
	int paren_depth = 0;
	int angle_depth = 0;
	std::size_t start = 0;
	for (std::size_t i = 0; i < text.size(); ++i) {
		char ch = text[i];
		if (ch == '(')
			++paren_depth;
		else if (ch == ')')
			--paren_depth;
		else if (ch == '<')
			++angle_depth;
		else if (ch == '>')
			--angle_depth;
		else if (ch == ',' && paren_depth == 0 && angle_depth == 0) {
			parts.push_back(trim_type_text_internal(text.substr(start, i - start)));
			start = i + 1;
		}
	}
	parts.push_back(trim_type_text_internal(text.substr(start)));
	return parts;
}

std::optional<std::vector<std::string>> consume_generic_type_arguments(
    const std::string &name, const std::string &prefix)
{
	const std::string open = prefix + "<";
	if (name.size() <= open.size() || name.rfind(open, 0) != 0 || name.back() != '>')
		return std::nullopt;

	int depth = 0;
	for (std::size_t i = prefix.size(); i < name.size(); ++i) {
		if (name[i] == '<')
			++depth;
		else if (name[i] == '>') {
			--depth;
			if (depth == 0 && i != name.size() - 1)
				return std::nullopt;
			if (depth < 0)
				return std::nullopt;
		}
	}
	if (depth != 0)
		return std::nullopt;

	auto inner = name.substr(prefix.size() + 1, name.size() - prefix.size() - 2);
	auto parts = split_type_arguments(inner);
	for (const auto &part : parts) {
		if (part.empty())
			return std::nullopt;
	}
	return parts;
}

std::string mangle_generic_suffix(
    const std::vector<std::string> &parameter_order,
    const std::unordered_map<std::string, PrimitiveType> &bindings)
{
	std::string suffix;
	for (const auto &name : parameter_order) {
		suffix += "__";
		auto it = bindings.find(name);
		std::string formatted = it != bindings.end() ? format_type(it->second) : name;
		// Replace characters illegal in symbol names with underscores so
		// `*T` -> `pT`, `(i32, bool)` -> `tup_i32_bool`-ish forms link.
		for (char ch : formatted) {
			if (ch == '*')
				suffix += 'p';
			else if (ch == '(' || ch == ')')
				; // skip
			else if (ch == ',' || ch == ' ' || ch == '<' || ch == '>')
				suffix += '_';
			else
				suffix += ch;
		}
	}
	return suffix;
}

} // namespace rexc
