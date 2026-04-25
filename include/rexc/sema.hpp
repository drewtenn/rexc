#pragma once

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

SemanticResult analyze_module(const ast::Module &module, Diagnostics &diagnostics);

} // namespace rexc
