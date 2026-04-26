#pragma once

// AST-to-IR lowering entry point.
#include "rexc/ast.hpp"
#include "rexc/ir.hpp"

namespace rexc {

struct LowerOptions {
	bool include_stdlib_prelude = true;
};

ir::Module lower_to_ir(const ast::Module &module, LowerOptions options = {});

} // namespace rexc
