#pragma once

// Parser entry point for turning source text into an AST module.
#include "rexc/ast.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/source.hpp"

#include <string>
#include <vector>

namespace rexc {

struct ParseOptions {
	std::vector<std::string> module_path;
};

struct ModuleLoadOptions {
	std::vector<std::string> package_paths;
};

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

ParseResult parse_source(const SourceFile &source, Diagnostics &diagnostics,
                         ParseOptions options = {});
ParseResult parse_file_tree(const std::string &entry_path, Diagnostics &diagnostics,
                            ModuleLoadOptions options = {});

} // namespace rexc
