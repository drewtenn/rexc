#pragma once

// Semantic analyzer entry point for name, type, and literal checks.
#include "rexc/ast.hpp"
#include "rexc/diagnostics.hpp"

namespace rexc {

class SemanticResult {
public:
	explicit SemanticResult(bool ok);

	bool ok() const;

private:
	bool ok_;
};

enum class StdlibSymbolPolicy {
	None,
	DefaultPrelude,
	All,
};

struct SemanticOptions {
	StdlibSymbolPolicy stdlib_symbols = StdlibSymbolPolicy::DefaultPrelude;
};

SemanticResult analyze_module(const ast::Module &module, Diagnostics &diagnostics,
                              SemanticOptions options = {});

} // namespace rexc
