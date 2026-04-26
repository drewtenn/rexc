#include "library.hpp"

namespace rexc::stdlib::core {
namespace {

PrimitiveType i32_type()
{
	return PrimitiveType{PrimitiveKind::SignedInteger, 32};
}

PrimitiveType str_type()
{
	return PrimitiveType{PrimitiveKind::Str};
}

PrimitiveType bool_type()
{
	return PrimitiveType{PrimitiveKind::Bool};
}

PrimitiveType u8_type()
{
	return PrimitiveType{PrimitiveKind::UnsignedInteger, 8};
}

PrimitiveType ptr_u8_type()
{
	return pointer_to(u8_type());
}

} // namespace

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions{
		FunctionDecl{Layer::Core, "strlen", {str_type()}, i32_type()},
		FunctionDecl{Layer::Core, "str_is_empty", {str_type()}, bool_type()},
		FunctionDecl{Layer::Core, "str_eq", {str_type(), str_type()}, bool_type()},
		FunctionDecl{Layer::Core, "str_starts_with", {str_type(), str_type()}, bool_type()},
		FunctionDecl{Layer::Core, "str_ends_with", {str_type(), str_type()}, bool_type()},
		FunctionDecl{Layer::Core, "str_contains", {str_type(), str_type()}, bool_type()},
		FunctionDecl{Layer::Core, "str_find", {str_type(), str_type()}, i32_type()},
		FunctionDecl{Layer::Core, "memset_u8", {ptr_u8_type(), u8_type(), i32_type()}, i32_type()},
		FunctionDecl{Layer::Core, "memcpy_u8", {ptr_u8_type(), ptr_u8_type(), i32_type()}, i32_type()},
		FunctionDecl{Layer::Core, "str_copy_to", {ptr_u8_type(), str_type(), i32_type()}, i32_type()},
		FunctionDecl{Layer::Core, "parse_i32", {str_type()}, i32_type()},
		FunctionDecl{Layer::Core, "parse_bool", {str_type()}, bool_type()},
	};
	return functions;
}

} // namespace rexc::stdlib::core
