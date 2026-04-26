#include "library.hpp"

namespace rexc::stdlib::std_layer {
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
		FunctionDecl{Layer::Std, "print", {str_type()}, i32_type()},
		FunctionDecl{Layer::Std, "println", {str_type()}, i32_type()},
		FunctionDecl{Layer::Std, "read_line", {}, str_type()},
		FunctionDecl{Layer::Std, "print_i32", {i32_type()}, i32_type()},
		FunctionDecl{Layer::Std, "println_i32", {i32_type()}, i32_type()},
		FunctionDecl{Layer::Std, "print_bool", {bool_type()}, i32_type()},
		FunctionDecl{Layer::Std, "println_bool", {bool_type()}, i32_type()},
		FunctionDecl{Layer::Std, "read_i32", {}, i32_type()},
		FunctionDecl{Layer::Std, "exit", {i32_type()}, i32_type()},
		FunctionDecl{Layer::Std, "panic", {str_type()}, i32_type()},
	};
	return functions;
}

} // namespace rexc::stdlib::std_layer
