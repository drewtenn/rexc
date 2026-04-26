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

} // namespace

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions{
		FunctionDecl{Layer::Core, "strlen", {str_type()}, i32_type()},
		FunctionDecl{Layer::Core, "str_is_empty", {str_type()}, bool_type()},
		FunctionDecl{Layer::Core, "str_eq", {str_type(), str_type()}, bool_type()},
		FunctionDecl{Layer::Core, "str_starts_with", {str_type(), str_type()}, bool_type()},
		FunctionDecl{Layer::Core, "str_contains", {str_type(), str_type()}, bool_type()},
		FunctionDecl{Layer::Core, "parse_i32", {str_type()}, i32_type()},
	};
	return functions;
}

} // namespace rexc::stdlib::core
