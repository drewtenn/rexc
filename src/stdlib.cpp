#include "rexc/stdlib.hpp"

#include <string>

namespace rexc::stdlib {
namespace {

PrimitiveType i32_type()
{
	return PrimitiveType{PrimitiveKind::SignedInteger, 32};
}

PrimitiveType str_type()
{
	return PrimitiveType{PrimitiveKind::Str};
}

} // namespace

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions{
		FunctionDecl{"print", {str_type()}, i32_type()},
		FunctionDecl{"println", {str_type()}, i32_type()},
		FunctionDecl{"read_line", {}, str_type()},
		FunctionDecl{"exit", {i32_type()}, i32_type()},
	};
	return functions;
}

const FunctionDecl *find_prelude_function(const std::string &name)
{
	for (const auto &function : prelude_functions()) {
		if (function.name == name)
			return &function;
	}
	return nullptr;
}

std::string hosted_runtime_assembly(CodegenTarget)
{
	return "";
}

} // namespace rexc::stdlib
