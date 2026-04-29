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

// FE-105: a stdlib unit may declare its own user structs (e.g. Arena)
// that user code can name in signatures and call into through the
// matching stdlib functions. We expose them through a separate list so
// sema can register them alongside user-declared structs without
// reparsing the stdlib sources at every call site.
struct StructFieldDecl {
	std::string name;
	PrimitiveType type;
};

struct StructDecl {
	Layer layer;
	std::string name;
	std::vector<StructFieldDecl> fields;
};

const std::vector<FunctionDecl> &stdlib_functions();
const std::vector<FunctionDecl> &prelude_functions();
const std::vector<StructDecl> &stdlib_structs();
const std::vector<std::string> &reserved_runtime_symbols();
const FunctionDecl *find_stdlib_function(const std::string &name);
const FunctionDecl *find_prelude_function(const std::string &name);
const StructDecl *find_stdlib_struct(const std::string &name);
std::string hosted_runtime_assembly(TargetTriple target);

} // namespace rexc::stdlib
