#include "library.hpp"

#include "stdlib/type_helpers.hpp"

namespace rexc::stdlib::std_layer {

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions{
		FunctionDecl{Layer::Std, "print", {str_type()}, i32_type()},
		FunctionDecl{Layer::Std, "println", {str_type()}, i32_type()},
		FunctionDecl{Layer::Std, "read_line", {}, str_type()},
		FunctionDecl{Layer::Std, "print_i32", {i32_type()}, i32_type()},
		FunctionDecl{Layer::Std, "println_i32", {i32_type()}, i32_type()},
		FunctionDecl{Layer::Std, "read_i32", {}, i32_type()},
		FunctionDecl{Layer::Std, "exit", {i32_type()}, i32_type()},
	};
	return functions;
}

} // namespace rexc::stdlib::std_layer
