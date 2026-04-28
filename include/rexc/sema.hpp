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
	// FE-013: when true, raw pointer dereference and calls to `extern fn`
	// must occur inside an `unsafe` block (or an `unsafe fn` body). The
	// CLI (src/main.cpp) sets this true; the library-level default is
	// permissive so that tests, IR/codegen pipelines, and the stdlib
	// emission path don't need to thread an `unsafe` policy through.
	bool enforce_unsafe_blocks = false;
};

SemanticResult analyze_module(const ast::Module &module, Diagnostics &diagnostics,
                              SemanticOptions options = {});

} // namespace rexc
