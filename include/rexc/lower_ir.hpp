#pragma once

// AST-to-IR lowering entry point.
#include "rexc/ast.hpp"
#include "rexc/ir.hpp"

namespace rexc {

enum class LowerStdlibSymbolPolicy {
	None,
	DefaultPrelude,
	All,
};

struct LowerOptions {
	LowerStdlibSymbolPolicy stdlib_symbols =
	    LowerStdlibSymbolPolicy::DefaultPrelude;
};

ir::Module lower_to_ir(const ast::Module &module, LowerOptions options = {});

} // namespace rexc
