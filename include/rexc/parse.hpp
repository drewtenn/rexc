#pragma once

// Parser entry point for turning source text into an AST module.
#include "rexc/ast.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/source.hpp"

namespace rexc {

class ParseResult {
public:
	ParseResult(bool ok, ast::Module module);

	bool ok() const;
	const ast::Module &module() const;
	ast::Module take_module();

private:
	bool ok_;
	ast::Module module_;
};

ParseResult parse_source(const SourceFile &source, Diagnostics &diagnostics);

} // namespace rexc
