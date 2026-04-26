#include "rexc/stdlib.hpp"

#include "rexc/codegen_arm64.hpp"
#include "rexc/codegen_x86.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "rexc/types.hpp"

#include "names.hpp"
#include "source.hpp"
#include "sys/runtime.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rexc::stdlib {
namespace {

std::optional<PrimitiveType> resolve_source_type(const ast::TypeName &type)
{
	return parse_primitive_type(type.name);
}

bool is_public_stdlib_function(const ast::Function &function)
{
	return !function.is_extern && !function.name.empty() && function.name.front() != '_';
}

void append_source_unit_declarations(std::vector<FunctionDecl> &target,
                                     const SourceUnit &unit)
{
	Diagnostics diagnostics;
	SourceFile source(unit.path, unit.source);
	auto parsed = parse_source(source, diagnostics);
	if (!parsed.ok())
		return;

	for (const auto &function : parsed.module().functions) {
		if (!is_public_stdlib_function(function))
			continue;

		auto return_type = resolve_source_type(function.return_type);
		if (!return_type)
			continue;

		std::vector<PrimitiveType> parameters;
		bool ok = true;
		for (const auto &parameter : function.parameters) {
			auto parameter_type = resolve_source_type(parameter.type);
			if (!parameter_type) {
				ok = false;
				break;
			}
			parameters.push_back(*parameter_type);
		}
		if (!ok)
			continue;

		target.push_back(FunctionDecl{unit.layer, function.name, std::move(parameters),
		                              *return_type});
	}
}

const std::unordered_set<std::string> &default_prelude_names()
{
	static const std::unordered_set<std::string> names{
	    "print", "println", "read_line",
	    "strlen", "str_eq", "str_is_empty", "str_starts_with", "str_ends_with",
	    "str_contains", "str_find",
	    "parse_i32", "parse_bool",
	    "print_i32", "println_i32", "print_bool", "println_bool", "print_char",
	    "println_char", "read_i32", "read_bool",
	    "panic"};
	return names;
}

bool is_default_prelude_function(const FunctionDecl &function)
{
	return default_prelude_names().find(function.name) != default_prelude_names().end();
}

const FunctionDecl *find_function_in(const std::vector<FunctionDecl> &functions,
                                     const std::string &name)
{
	for (const auto &function : functions) {
		if (function.name == name)
			return &function;
		if (auto path = stdlib_path_for_symbol(function.name);
		    path && canonical_path(*path) == name)
			return &function;
	}
	return nullptr;
}

std::string portable_stdlib_assembly(CodegenTarget target)
{
	Diagnostics diagnostics;
	SourceFile source("stdlib.rx", portable_stdlib_source());
	auto parsed = parse_source(source, diagnostics);
	if (!parsed.ok())
		return "# failed to parse stdlib.rx\n# " + diagnostics.format() + "\n";

	SemanticOptions semantic_options;
	semantic_options.include_stdlib_prelude = false;
	auto sema = analyze_module(parsed.module(), diagnostics, semantic_options);
	if (!sema.ok())
		return "# failed to analyze stdlib.rx\n# " + diagnostics.format() + "\n";

	LowerOptions lower_options;
	lower_options.include_stdlib_prelude = false;
	auto lowered = lower_to_ir(parsed.module(), lower_options);

	CodegenResult emitted =
		target == CodegenTarget::ARM64_MACOS
			? emit_arm64_macos_assembly(lowered, diagnostics)
			: emit_x86_assembly(lowered, diagnostics, target);
	if (!emitted.ok())
		return "# failed to emit stdlib.rx\n# " + diagnostics.format() + "\n";
	return emitted.assembly();
}

std::string sys_runtime_assembly(TargetTriple target)
{
	switch (target) {
	case TargetTriple::I386Linux:
	case TargetTriple::I386Elf:
		return i386_linux_hosted_runtime_assembly();
	case TargetTriple::I386Drunix:
		return i386_drunix_hosted_runtime_assembly();
	case TargetTriple::X86_64Linux:
	case TargetTriple::X86_64Elf:
		return x86_64_linux_hosted_runtime_assembly();
	case TargetTriple::ARM64Macos:
		return arm64_macos_hosted_runtime_assembly();
	}
	return "";
}

} // namespace

const std::vector<FunctionDecl> &stdlib_functions()
{
	static const std::vector<FunctionDecl> functions = [] {
		std::vector<FunctionDecl> result;
		for (const auto &unit : portable_stdlib_source_units())
			append_source_unit_declarations(result, unit);
		return result;
	}();
	return functions;
}

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions = [] {
		std::vector<FunctionDecl> result;
		for (const auto &function : stdlib_functions()) {
			if (is_default_prelude_function(function))
				result.push_back(function);
		}
		return result;
	}();
	return functions;
}

const FunctionDecl *find_stdlib_function(const std::string &name)
{
	return find_function_in(stdlib_functions(), name);
}

const FunctionDecl *find_prelude_function(const std::string &name)
{
	return find_function_in(prelude_functions(), name);
}

std::string hosted_runtime_assembly(TargetTriple target)
{
	return sys_runtime_assembly(target) + "\n" +
	       portable_stdlib_assembly(codegen_target(target));
}

} // namespace rexc::stdlib
