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

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rexc::stdlib {
namespace {

std::string trim_type_name(const std::string &value)
{
	auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
		return std::isspace(ch) != 0;
	});
	auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
		return std::isspace(ch) != 0;
	}).base();
	if (begin >= end)
		return "";
	return std::string(begin, end);
}

using UserStructTypeMap = std::unordered_map<std::string, PrimitiveType>;

// FE-105: a stdlib unit may declare its own structs (e.g. `Arena`) and
// reference them in function signatures (e.g. `*Arena`). The built-in
// `parse_primitive_type` only knows about language primitives, so we
// pass it a per-unit map of user-struct names → resolved
// `UserStruct` PrimitiveType (with `bits` set from the laid-out struct
// size) and synthesize a `Pointer` wrapping for the `*Arena` form when
// the primitive parser returns nullopt. Matching the bits exactly is
// what lets sema treat the registry-side `*Arena` and the user-side
// `*Arena` as the same PrimitiveType under operator==.
std::optional<PrimitiveType> resolve_source_type(
    const ast::TypeName &type,
    const UserStructTypeMap &user_structs)
{
	auto primitive = parse_primitive_type(type.name);
	if (primitive)
		return primitive;

	// Direct user struct: "Arena" (or " Arena" defensively).
	std::string trimmed = trim_type_name(type.name);
	if (auto it = user_structs.find(trimmed); it != user_structs.end())
		return it->second;

	// Pointer to user struct: "*Arena" or "* Arena".
	if (!trimmed.empty() && trimmed.front() == '*') {
		std::string inner = trim_type_name(trimmed.substr(1));
		if (auto it = user_structs.find(inner); it != user_structs.end())
			return pointer_to(it->second);
	}

	return std::nullopt;
}

bool is_public_stdlib_function(const ast::Function &function)
{
	return !function.is_extern && !function.name.empty() && function.name.front() != '_';
}

std::size_t align_up(std::size_t value, std::size_t alignment)
{
	if (alignment <= 1)
		return value;
	return (value + alignment - 1) / alignment * alignment;
}

// Mirror sema::compute_struct_layout: walk fields, align each one to its
// natural alignment, sum the sizes, then round the total up to the
// struct's alignment. Used to set the `bits` of the UserStruct
// PrimitiveType so it matches sema's view of the same struct.
std::size_t compute_struct_size_bytes(const std::vector<StructFieldDecl> &fields)
{
	std::size_t offset = 0;
	std::size_t struct_alignment = 1;
	for (const auto &field : fields) {
		std::size_t field_alignment = type_alignment_bytes(field.type).value_or(1u);
		std::size_t field_size = type_size_bytes(field.type).value_or(0u);
		offset = align_up(offset, field_alignment);
		offset += field_size;
		if (field_alignment > struct_alignment)
			struct_alignment = field_alignment;
	}
	return align_up(offset, struct_alignment);
}

// Two-pass per-unit registration: collect struct declarations first so
// every PrimitiveType emitted for `*Arena` etc. carries the laid-out
// `bits`, then resolve function signatures using the populated map.
struct ResolvedUnit {
	UserStructTypeMap struct_types;
	std::vector<StructDecl> structs;
	std::vector<FunctionDecl> functions;
};

ResolvedUnit resolve_source_unit(const SourceUnit &unit)
{
	ResolvedUnit resolved;
	Diagnostics diagnostics;
	SourceFile source(unit.path, unit.source);
	auto parsed = parse_source(source, diagnostics);
	if (!parsed.ok())
		return resolved;

	// Pass 1: stub each struct's type without bits so field types that
	// reference a sibling struct still resolve. Bits are filled in
	// after layout below.
	for (const auto &declaration : parsed.module().structs) {
		if (declaration.name.empty())
			continue;
		if (!declaration.generic_parameters.empty())
			continue;
		PrimitiveType placeholder;
		placeholder.kind = PrimitiveKind::UserStruct;
		placeholder.name = declaration.name;
		resolved.struct_types[declaration.name] = std::move(placeholder);
	}

	// Pass 2: resolve fields, compute layout, finalize each struct's
	// PrimitiveType bits in the map.
	for (const auto &declaration : parsed.module().structs) {
		if (declaration.name.empty())
			continue;
		if (!declaration.generic_parameters.empty())
			continue;
		StructDecl decl;
		decl.layer = unit.layer;
		decl.name = declaration.name;
		bool ok = true;
		for (const auto &field : declaration.fields) {
			auto field_type = resolve_source_type(field.type, resolved.struct_types);
			if (!field_type) {
				ok = false;
				break;
			}
			decl.fields.push_back(StructFieldDecl{field.name, *field_type});
		}
		if (!ok) {
			resolved.struct_types.erase(declaration.name);
			continue;
		}
		std::size_t size = compute_struct_size_bytes(decl.fields);
		auto &type = resolved.struct_types[declaration.name];
		type.bits = static_cast<int>(size * 8);
		resolved.structs.push_back(std::move(decl));
	}

	// Pass 3: resolve function signatures — `*Arena` now carries the
	// matching pointee bits.
	for (const auto &function : parsed.module().functions) {
		if (!is_public_stdlib_function(function))
			continue;

		auto return_type = resolve_source_type(function.return_type, resolved.struct_types);
		if (!return_type)
			continue;

		std::vector<PrimitiveType> parameters;
		bool ok = true;
		for (const auto &parameter : function.parameters) {
			auto parameter_type = resolve_source_type(parameter.type, resolved.struct_types);
			if (!parameter_type) {
				ok = false;
				break;
			}
			parameters.push_back(*parameter_type);
		}
		if (!ok)
			continue;

		resolved.functions.push_back(FunctionDecl{unit.layer, function.name,
		                                          std::move(parameters), *return_type});
	}

	return resolved;
}

void append_source_unit_declarations(std::vector<FunctionDecl> &target,
                                     const SourceUnit &unit)
{
	auto resolved = resolve_source_unit(unit);
	for (auto &function : resolved.functions)
		target.push_back(std::move(function));
}

void append_source_unit_structs(std::vector<StructDecl> &target,
                                const SourceUnit &unit)
{
	auto resolved = resolve_source_unit(unit);
	for (auto &declaration : resolved.structs)
		target.push_back(std::move(declaration));
}

void append_source_unit_symbol_names(std::vector<std::string> &target,
                                     const SourceUnit &unit)
{
	Diagnostics diagnostics;
	SourceFile source(unit.path, unit.source);
	auto parsed = parse_source(source, diagnostics);
	if (!parsed.ok())
		return;

	for (const auto &function : parsed.module().functions) {
		if (!function.name.empty())
			target.push_back(function.name);
	}
	for (const auto &buffer : parsed.module().static_buffers) {
		if (!buffer.name.empty())
			target.push_back(buffer.name);
	}
	for (const auto &scalar : parsed.module().static_scalars) {
		if (!scalar.name.empty())
			target.push_back(scalar.name);
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
	semantic_options.stdlib_symbols = StdlibSymbolPolicy::None;
	// FE-013: stdlib code IS the trusted runtime — its raw deref and
	// extern fn calls cannot themselves be wrapped in unsafe.
	semantic_options.enforce_unsafe_blocks = false;
	auto sema = analyze_module(parsed.module(), diagnostics, semantic_options);
	if (!sema.ok())
		return "# failed to analyze stdlib.rx\n# " + diagnostics.format() + "\n";

	LowerOptions lower_options;
	lower_options.stdlib_symbols = LowerStdlibSymbolPolicy::None;
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

const std::vector<StructDecl> &stdlib_structs()
{
	static const std::vector<StructDecl> structs = [] {
		std::vector<StructDecl> result;
		for (const auto &unit : portable_stdlib_source_units())
			append_source_unit_structs(result, unit);
		return result;
	}();
	return structs;
}

const StructDecl *find_stdlib_struct(const std::string &name)
{
	for (const auto &declaration : stdlib_structs()) {
		if (declaration.name == name)
			return &declaration;
	}
	return nullptr;
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

const std::vector<std::string> &reserved_runtime_symbols()
{
	static const std::vector<std::string> symbols = [] {
		std::vector<std::string> result;
		for (const auto &unit : portable_stdlib_source_units())
			append_source_unit_symbol_names(result, unit);
		result.push_back("__rexc_argc");
		result.push_back("__rexc_argv");
		result.push_back("__rexc_envp");
		result.push_back("__rexc_empty_string");
		result.push_back("environ");
		return result;
	}();
	return symbols;
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
