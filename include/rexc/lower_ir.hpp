#pragma once

// AST-to-IR lowering entry point.
#include "rexc/ast.hpp"
#include "rexc/ir.hpp"

namespace rexc {

ir::Module lower_to_ir(const ast::Module &module);

} // namespace rexc
