#pragma once

#include "rexc/codegen.hpp"
#include "rexc/types.hpp"

#include <string>
#include <vector>

namespace rexc::stdlib {

struct FunctionDecl {
	std::string name;
	std::vector<PrimitiveType> parameters;
	PrimitiveType return_type;
};

const std::vector<FunctionDecl> &prelude_functions();
const FunctionDecl *find_prelude_function(const std::string &name);
std::string hosted_runtime_assembly(CodegenTarget target);

} // namespace rexc::stdlib
