#include "library.hpp"

#include "stdlib/type_helpers.hpp"

namespace rexc::stdlib::core {

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions{
		FunctionDecl{Layer::Core, "strlen", {str_type()}, i32_type()},
		FunctionDecl{Layer::Core, "str_eq", {str_type(), str_type()},
		             PrimitiveType{PrimitiveKind::Bool}},
		FunctionDecl{Layer::Core, "parse_i32", {str_type()}, i32_type()},
	};
	return functions;
}

} // namespace rexc::stdlib::core
