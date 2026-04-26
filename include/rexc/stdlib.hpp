#pragma once

#include "rexc/codegen.hpp"
#include "rexc/target.hpp"
#include "rexc/types.hpp"

#include <string>
#include <vector>

namespace rexc::stdlib {

enum class Layer { Core, Alloc, Std, Sys };

struct FunctionDecl {
	Layer layer;
	std::string name;
	std::vector<PrimitiveType> parameters;
	PrimitiveType return_type;
};

const std::vector<FunctionDecl> &prelude_functions();
const FunctionDecl *find_prelude_function(const std::string &name);
std::string hosted_runtime_assembly(TargetTriple target);

} // namespace rexc::stdlib
