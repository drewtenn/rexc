#include "library.hpp"

namespace rexc::stdlib::alloc {
namespace {

PrimitiveType i32_type()
{
	return PrimitiveType{PrimitiveKind::SignedInteger, 32};
}

PrimitiveType u8_type()
{
	return PrimitiveType{PrimitiveKind::UnsignedInteger, 8};
}

PrimitiveType ptr_u8_type()
{
	return pointer_to(u8_type());
}

PrimitiveType str_type()
{
	return PrimitiveType{PrimitiveKind::Str};
}

PrimitiveType bool_type()
{
	return PrimitiveType{PrimitiveKind::Bool};
}

} // namespace

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions{
		FunctionDecl{Layer::Alloc, "alloc_bytes", {i32_type()}, ptr_u8_type()},
		FunctionDecl{Layer::Alloc, "alloc_str_copy", {str_type()}, str_type()},
		FunctionDecl{Layer::Alloc, "alloc_str_concat", {str_type(), str_type()}, str_type()},
		FunctionDecl{Layer::Alloc, "alloc_i32_to_str", {i32_type()}, str_type()},
		FunctionDecl{Layer::Alloc, "alloc_bool_to_str", {bool_type()}, str_type()},
		FunctionDecl{Layer::Alloc, "alloc_remaining", {}, i32_type()},
		FunctionDecl{Layer::Alloc, "alloc_reset", {}, i32_type()},
	};
	return functions;
}

} // namespace rexc::stdlib::alloc
