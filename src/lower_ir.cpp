// AST-to-IR lowering.
//
// This file converts semantically checked source syntax into the smaller typed
// representation consumed by codegen. It resolves type names into PrimitiveType
// values, preserves expression and statement structure needed by the backend,
// and maps source locals/parameters into IR declarations while assuming sema
// has already rejected invalid names, types, and control flow.
#include "rexc/lower_ir.hpp"
#include "rexc/stdlib.hpp"
#include "rexc/types.hpp"

#include "names.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rexc {
namespace {

PrimitiveType i32_type()
{
	return PrimitiveType{PrimitiveKind::SignedInteger, 32};
}

PrimitiveType bool_type()
{
	return PrimitiveType{PrimitiveKind::Bool};
}

PrimitiveType u8_type()
{
	return PrimitiveType{PrimitiveKind::UnsignedInteger, 8};
}

bool is_comparison_operator(const std::string &op)
{
	return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
	       op == ">=";
}

bool is_logical_operator(const std::string &op)
{
	return op == "&&" || op == "||";
}

std::optional<std::size_t> parse_tuple_field_index(const std::string &field)
{
	if (field.empty())
		return std::nullopt;
	std::size_t value = 0;
	for (char ch : field) {
		if (ch < '0' || ch > '9')
			return std::nullopt;
		value = value * 10u + static_cast<std::size_t>(ch - '0');
	}
	return value;
}

std::optional<std::string> slice_helper_type_suffix(PrimitiveType type)
{
	if (type == i32_type())
		return std::string("i32");
	if (type == u8_type())
		return std::string("u8");
	return std::nullopt;
}

struct FunctionInfo {
	ir::Type return_type = i32_type();
	std::vector<ir::Type> parameter_types;
	std::string symbol_name;
	// FE-103: when set, this function is a generic template — its
	// FunctionInfo holds the *pattern* return/parameter types (with type
	// variables present), and lowering goes via monomorphization rather
	// than a direct call lookup.
	std::vector<std::string> generic_parameters;
	const ast::Function *ast_function = nullptr;
};

struct GlobalInfo {
	ir::Type type = PrimitiveType{PrimitiveKind::Str};
	std::string symbol_name;
};

enum class ImportKind { Function, Global, Module };

struct ImportInfo {
	ImportKind kind;
	std::string target_key;
	std::vector<std::string> target_path;
};

class Lowerer {
public:
	Lowerer(const ast::Module &module, LowerOptions options)
		: module_(module), options_(options)
	{
	}

	ir::Module run()
	{
		register_type_tables();
		compute_struct_layouts();
		compute_enum_layouts();
		build_static_table();
		build_function_table();
		build_import_table();

		ir::Module lowered;
		for (const auto &buffer : module_.static_buffers)
			lowered.static_buffers.push_back(lower_static_buffer(buffer));
		for (const auto &scalar : module_.static_scalars)
			lowered.static_scalars.push_back(lower_static_scalar(scalar));
		for (const auto &function : module_.functions) {
			// FE-103: generic templates are emitted on demand by call sites
			// rather than as standalone functions. Each unique instantiation
			// gets its own mangled symbol via lower_monomorph().
			if (!function.generic_parameters.empty())
				continue;
			lowered.functions.push_back(lower_function(function));
		}
		// FE-103: drain the monomorph queue. Lowering an instantiation may
		// discover more generic calls, so loop until fixpoint.
		while (!monomorph_queue_.empty()) {
			auto pending = std::move(monomorph_queue_.back());
			monomorph_queue_.pop_back();
			lowered.functions.push_back(lower_monomorph(pending));
		}
		return lowered;
	}

private:
	using Locals = std::unordered_map<std::string, ir::Type>;
	using ImportScope = std::unordered_map<std::string, ImportInfo>;

	// FE-103: a deferred generic-function instantiation queued by a call
	// site. The Lowerer drains a queue of these after the main lowering
	// pass, emitting one mangled ir::Function per (template, bindings) pair.
	struct PendingMonomorph {
		const ast::Function *ast_function = nullptr;
		std::string mangled_name;
		std::vector<std::string> module_path;
		std::unordered_map<std::string, ir::Type> bindings;
	};

	struct StructFieldLayout {
		std::string name;
		ir::Type type;
		std::size_t offset = 0;
	};

	struct StructLayout {
		std::vector<StructFieldLayout> fields;
		std::size_t total_size = 0;
		std::size_t alignment = 1;
		std::optional<StructFieldLayout> field(const std::string &name) const
		{
			for (const auto &f : fields)
				if (f.name == name)
					return f;
			return std::nullopt;
		}
	};

	struct EnumLayoutInfo {
		EnumLayout layout;
	};

	struct EnumVariantLowerInfo {
		ir::Type enum_type = PrimitiveType{PrimitiveKind::UserEnum};
		EnumVariantLayout variant;
		std::size_t payload_offset = 4;
		std::size_t total_size = 4;
	};

	std::size_t size_in_bytes(const ir::Type &type) const
	{
		switch (type.kind) {
		case PrimitiveKind::SignedInteger:
		case PrimitiveKind::UnsignedInteger:
			return static_cast<std::size_t>(type.bits) / 8u;
		case PrimitiveKind::Bool:
			return 1u;
		case PrimitiveKind::Char:
			return 4u;
		case PrimitiveKind::Pointer:
		case PrimitiveKind::Str:
		case PrimitiveKind::OwnedStr:
		case PrimitiveKind::Slice:
		case PrimitiveKind::Vector:
		case PrimitiveKind::Option:
		case PrimitiveKind::Result:
			return 8u; // assume 64-bit pointer-shaped layout
		case PrimitiveKind::Tuple:
			return type.bits > 0 ? static_cast<std::size_t>(type.bits) / 8u : 0u;
		case PrimitiveKind::UserStruct: {
			auto it = struct_layouts_.find(type.name);
			return it == struct_layouts_.end() ? 0u : it->second.total_size;
		}
		case PrimitiveKind::UserEnum: {
			auto it = enum_layouts_.find(type.name);
			if (it != enum_layouts_.end())
				return it->second.layout.total_size;
			return type.bits > 0 ? static_cast<std::size_t>(type.bits) / 8u : 0u;
		}
		}
		return 0u;
	}

	std::size_t alignment_of(const ir::Type &type) const
	{
		if (type.kind == PrimitiveKind::UserStruct) {
			auto it = struct_layouts_.find(type.name);
			if (it != struct_layouts_.end())
				return it->second.alignment;
		}
		if (type.kind == PrimitiveKind::UserEnum) {
			auto it = enum_layouts_.find(type.name);
			if (it != enum_layouts_.end())
				return it->second.layout.alignment;
		}
		if (type.kind == PrimitiveKind::Tuple) {
			auto elements = tuple_elements(type);
			if (elements)
				return layout_tuple_elements(*elements).alignment;
		}
		// Natural alignment: align to the type's own size. Matches what x86_64
		// and AArch64 require for non-faulting loads/stores. Doesn't yet model
		// per-target overrides (e.g., 32-bit pointers on i386 align to 4).
		std::size_t size = size_in_bytes(type);
		return size == 0 ? 1 : size;
	}

	static std::size_t align_up(std::size_t value, std::size_t alignment)
	{
		if (alignment <= 1)
			return value;
		return (value + alignment - 1) / alignment * alignment;
	}

	void register_type_tables()
	{
		for (const auto &decl : module_.structs) {
			// FE-103: generic struct templates are not registered as
			// concrete layouts; each `Box<i32>`-style instantiation gets
			// its own mangled entry created on demand by lower_type_name.
			if (decl.generic_parameters.empty()) {
				struct_layouts_.emplace(decl.name, StructLayout{});
			} else {
				generic_struct_templates_[decl.name] = &decl;
			}
		}
		for (const auto &decl : module_.enums)
			enum_layouts_.emplace(decl.name, EnumLayoutInfo{});
	}

	void compute_struct_layouts()
	{
		for (const auto &decl : module_.structs) {
			if (!decl.generic_parameters.empty())
				continue; // FE-103: generic templates have no layout
			auto it = struct_layouts_.find(decl.name);
			if (it == struct_layouts_.end())
				continue;
			std::size_t offset = 0;
			std::size_t struct_alignment = 1;
			for (const auto &field : decl.fields) {
				ir::Type field_type = lower_type(field.type);
				std::size_t field_alignment = alignment_of(field_type);
				offset = align_up(offset, field_alignment);
				it->second.fields.push_back(
				    StructFieldLayout{field.name, field_type, offset});
				offset += size_in_bytes(field_type);
				if (field_alignment > struct_alignment)
					struct_alignment = field_alignment;
			}
			// Round total size up to the struct's alignment so arrays of
			// structs work correctly.
			it->second.total_size = align_up(offset, struct_alignment);
			it->second.alignment = struct_alignment;
		}
	}

	// FE-103: instantiate a generic struct template (e.g. `Box<i32>`) at
	// IR-lower time. Mangles the same way sema does so the two layers
	// agree on the type identity.
	std::string instantiate_generic_struct_layout(
	    const ast::StructDecl &tpl,
	    const std::vector<std::string> &type_arg_names)
	{
		std::unordered_map<std::string, ir::Type> bindings;
		for (std::size_t i = 0;
		     i < tpl.generic_parameters.size() && i < type_arg_names.size();
		     ++i)
			bindings[tpl.generic_parameters[i]] = lower_type_name(type_arg_names[i]);
		std::string mangled =
		    tpl.name + mangle_generic_suffix(tpl.generic_parameters, bindings);
		if (struct_layouts_.find(mangled) != struct_layouts_.end())
			return mangled;
		// FE-103: pre-register an empty layout under the mangled name so a
		// recursive lookup of the same instantiation (e.g. *Tree<T> field
		// inside Tree<T>) finds the in-progress entry and terminates rather
		// than recursing infinitely. The final assignment at the end of
		// this function overwrites the placeholder with the computed layout.
		// Pointer self-references resolve fine because pointer codegen only
		// needs the pointee's name, not its size.
		struct_layouts_[mangled] = StructLayout{};
		StructLayout layout;
		std::size_t offset = 0;
		std::size_t struct_alignment = 1;
		auto saved = current_type_substitutions_;
		current_type_substitutions_ = bindings;
		for (const auto &field : tpl.fields) {
			ir::Type field_type = lower_type(field.type);
			std::size_t field_alignment = alignment_of(field_type);
			offset = align_up(offset, field_alignment);
			layout.fields.push_back(
			    StructFieldLayout{field.name, field_type, offset});
			offset += size_in_bytes(field_type);
			if (field_alignment > struct_alignment)
				struct_alignment = field_alignment;
		}
		layout.total_size = align_up(offset, struct_alignment);
		layout.alignment = struct_alignment;
		current_type_substitutions_ = std::move(saved);
		struct_layouts_[mangled] = std::move(layout);
		return mangled;
	}

	void compute_enum_layouts()
	{
		for (const auto &decl : module_.enums) {
			auto it = enum_layouts_.find(decl.name);
			if (it == enum_layouts_.end())
				continue;
			std::vector<EnumVariantSpec> specs;
			specs.reserve(decl.variants.size());
			for (const auto &variant : decl.variants) {
				std::vector<PrimitiveType> payload_types;
				payload_types.reserve(variant.payload_types.size());
				for (const auto &payload_type : variant.payload_types)
					payload_types.push_back(lower_type(payload_type));
				specs.push_back(EnumVariantSpec{variant.name, std::move(payload_types)});
			}

			it->second.layout = layout_enum_variants(specs);
			ir::Type enum_type = user_enum_type(
				decl.name, static_cast<int>(it->second.layout.total_size * 8));
			for (const auto &variant : it->second.layout.variants) {
				EnumVariantLowerInfo info;
				info.enum_type = enum_type;
				info.variant = variant;
				info.payload_offset = it->second.layout.payload_offset;
				info.total_size = it->second.layout.total_size;
				enum_variants_by_name_[variant.name].push_back(info);
				enum_variants_by_qualified_[decl.name + "::" + variant.name] =
					std::move(info);
			}
		}
	}

	ir::Type lower_type(const ast::TypeName &type)
	{
		return lower_type_name(type.name);
	}

	ir::Type lower_type_name(const std::string &name)
	{
		// FE-103: while monomorphizing a generic body, type-variable names
		// resolve to the bound concrete type set up by lower_monomorph().
		if (auto it = current_type_substitutions_.find(name);
		    it != current_type_substitutions_.end())
			return it->second;
		auto primitive_type = parse_primitive_type(name);
		if (primitive_type)
			return *primitive_type;
		if (!name.empty() && name.front() == '*')
			return pointer_to(lower_type_name(name.substr(1)));
		if (auto tuple_names = split_tuple_type_name(name)) {
			std::vector<PrimitiveType> elements;
			elements.reserve(tuple_names->size());
			for (const auto &element_name : *tuple_names)
				elements.push_back(lower_type_name(element_name));
			return tuple_type(std::move(elements));
		}
		if (auto layout = struct_layouts_.find(name); layout != struct_layouts_.end())
			return user_struct_type(name, static_cast<int>(layout->second.total_size * 8));
		if (auto layout = enum_layouts_.find(name); layout != enum_layouts_.end())
			return user_enum_type(name, static_cast<int>(layout->second.layout.total_size * 8));
		// FE-103: lazily instantiate `Box<i32>`-shaped generic struct types.
		if (auto open = name.find('<');
		    open != std::string::npos && !name.empty() && name.back() == '>') {
			std::string base = name.substr(0, open);
			auto template_it = generic_struct_templates_.find(base);
			if (template_it != generic_struct_templates_.end()) {
				auto args = consume_generic_type_arguments(name, base);
				if (args) {
					// FE-103.1: defer mangling when an arg is itself an
					// unbound generic type variable (e.g. `Box<T>` inside
					// a generic fn signature). Eagerly mangling produces a
					// `Box__T` placeholder layout that downstream
					// substitute_generics can't re-walk to `Box__i32`.
					// Returning the angle-bracket name here lets later
					// passes (lower_monomorph, sema-driven re-resolve)
					// instantiate with concrete bindings.
					bool has_unbound = false;
					for (const auto &arg : *args) {
						if (current_type_substitutions_.find(arg) !=
						    current_type_substitutions_.end()) {
							const auto &bound =
							    current_type_substitutions_.at(arg);
							if (is_user_struct(bound) && bound.name == arg) {
								has_unbound = true;
								break;
							}
						}
					}
					if (has_unbound)
						return user_struct_type(name);
					std::string mangled = instantiate_generic_struct_layout(
					    *template_it->second, *args);
					auto layout = struct_layouts_.find(mangled);
					if (layout != struct_layouts_.end())
						return user_struct_type(
						    mangled,
						    static_cast<int>(layout->second.total_size * 8));
				}
			}
		}
		// FE-103: bare reference to a generic struct (e.g. `Box` in a
		// struct literal whose concrete instantiation comes from context).
		// Return a sentinel UserStruct; the consumer (lower_struct_literal)
		// is expected to swap in the monomorph using the expected type.
		if (generic_struct_templates_.count(name) > 0)
			return user_struct_type(name);
		throw std::runtime_error("unknown primitive type in IR lowering: " + name);
	}

	const std::vector<stdlib::FunctionDecl> &bare_stdlib_functions() const
	{
		if (options_.stdlib_symbols == LowerStdlibSymbolPolicy::All)
			return stdlib::stdlib_functions();
		return stdlib::prelude_functions();
	}

	bool include_stdlib_symbols() const
	{
		return options_.stdlib_symbols != LowerStdlibSymbolPolicy::None;
	}

	bool include_bare_stdlib_symbols() const
	{
		return options_.stdlib_symbols != LowerStdlibSymbolPolicy::None;
	}

	void note_module_path(const std::vector<std::string> &module_path)
	{
		for (std::size_t size = 1; size <= module_path.size(); ++size) {
			std::vector<std::string> prefix(module_path.begin(),
			                                module_path.begin() + size);
			modules_.insert(canonical_path(prefix));
		}
	}

	void note_item_path(const std::vector<std::string> &path)
	{
		if (path.empty())
			return;
		std::vector<std::string> module_path(path.begin(), path.end() - 1);
		note_module_path(module_path);
	}

	void build_static_table()
	{
		for (const auto &buffer : module_.static_buffers) {
			ir::Type element_type = lower_type(buffer.element_type);
			ir::Type global_type =
			    element_type == u8_type() ? PrimitiveType{PrimitiveKind::Str}
			                              : pointer_to(element_type);
			globals_[canonical_item_path(buffer.module_path, buffer.name)] =
				GlobalInfo{global_type,
				           symbol_item_path(buffer.module_path, buffer.name)};
		}
		for (const auto &scalar : module_.static_scalars)
			globals_[canonical_item_path(scalar.module_path, scalar.name)] =
				GlobalInfo{lower_type(scalar.type),
				           symbol_item_path(scalar.module_path, scalar.name)};
		for (const auto &buffer : module_.static_buffers)
			note_module_path(buffer.module_path);
		for (const auto &scalar : module_.static_scalars)
			note_module_path(scalar.module_path);
	}

	void build_function_table()
	{
		if (include_stdlib_symbols()) {
			for (const auto &function : stdlib::stdlib_functions()) {
				if (auto path = stdlib_path_for_symbol(function.name)) {
					note_item_path(*path);
					FunctionInfo path_info;
					path_info.return_type = function.return_type;
					path_info.parameter_types = function.parameters;
					path_info.symbol_name = function.name;
					functions_[canonical_path(*path)] = std::move(path_info);
				}
			}
			if (include_bare_stdlib_symbols()) {
				for (const auto &function : bare_stdlib_functions()) {
					FunctionInfo info;
					info.return_type = function.return_type;
					info.parameter_types = function.parameters;
					info.symbol_name = function.name;
					functions_[function.name] = std::move(info);
				}
			}
		}

		for (const auto &function : module_.functions) {
			FunctionInfo info;
			// FE-103: register the function's generic parameters as
			// "type-variable substitutions to themselves" so lower_type
			// resolves their occurrences in the *pattern* type without
			// erroring. Bindings to concrete types happen in lower_monomorph.
			if (!function.generic_parameters.empty()) {
				current_type_substitutions_.clear();
				for (const auto &name : function.generic_parameters)
					current_type_substitutions_[name] = user_struct_type(name);
			}
			info.return_type = lower_type(function.return_type);
			for (const auto &parameter : function.parameters)
				info.parameter_types.push_back(lower_type(parameter.type));
			info.symbol_name = symbol_item_path(function.module_path, function.name);
			info.generic_parameters = function.generic_parameters;
			info.ast_function = &function;
			functions_[canonical_item_path(function.module_path, function.name)] =
				std::move(info);
			note_module_path(function.module_path);
			current_type_substitutions_.clear();
		}
	}

	void build_import_table()
	{
		for (const auto &use : module_.uses) {
			if (use.import_path.empty())
				continue;

			std::string target_key = canonical_path(use.import_path);
			ImportKind kind;
			if (functions_.find(target_key) != functions_.end()) {
				kind = ImportKind::Function;
			} else if (globals_.find(target_key) != globals_.end()) {
				kind = ImportKind::Global;
			} else if (modules_.find(target_key) != modules_.end()) {
				kind = ImportKind::Module;
			} else {
				continue;
			}

			imports_[canonical_path(use.module_path)][use.import_path.back()] =
				ImportInfo{kind, target_key, use.import_path};
		}
	}

	const std::vector<std::string> &current_module_path() const
	{
		static const std::vector<std::string> empty;
		return current_module_path_ != nullptr ? *current_module_path_ : empty;
	}

	const ImportInfo *find_import(const std::string &alias) const
	{
		auto scope = imports_.find(canonical_path(current_module_path()));
		if (scope == imports_.end())
			return nullptr;
		auto import = scope->second.find(alias);
		return import != scope->second.end() ? &import->second : nullptr;
	}

	const FunctionInfo *find_function(const std::vector<std::string> &path) const
	{
		auto it = functions_.find(canonical_path(path));
		return it != functions_.end() ? &it->second : nullptr;
	}

	const FunctionInfo *resolve_function(const std::vector<std::string> &path) const
	{
		if (path.empty())
			return nullptr;

		if (path.size() == 1) {
			if (auto import = find_import(path[0]);
			    import != nullptr && import->kind == ImportKind::Function) {
				auto it = functions_.find(import->target_key);
				return it != functions_.end() ? &it->second : nullptr;
			}

			if (auto function = find_function(item_path(current_module_path(), path[0])))
				return function;
			return find_function(path);
		}

		if (auto import = find_import(path[0]);
		    import != nullptr && import->kind == ImportKind::Module) {
			auto expanded = import->target_path;
			expanded.insert(expanded.end(), path.begin() + 1, path.end());
			if (auto function = find_function(expanded))
				return function;
		}

		auto relative = current_module_path();
		relative.insert(relative.end(), path.begin(), path.end());
		if (auto function = find_function(relative))
			return function;
		return find_function(path);
	}

	const GlobalInfo *find_global(const std::vector<std::string> &path) const
	{
		auto it = globals_.find(canonical_path(path));
		return it != globals_.end() ? &it->second : nullptr;
	}

	const GlobalInfo *resolve_global(const std::string &name) const
	{
		if (auto import = find_import(name);
		    import != nullptr && import->kind == ImportKind::Global) {
			auto it = globals_.find(import->target_key);
			return it != globals_.end() ? &it->second : nullptr;
		}

		if (auto global = find_global(item_path(current_module_path(), name)))
			return global;
		return find_global({name});
	}

	std::unique_ptr<ir::Value> lower_expr(
		const ast::Expr &expr, const Locals &locals,
		std::optional<ir::Type> expected = std::nullopt)
	{
		// Sema has already proven types match; this mirrors its expected-type
		// flow so literal IR nodes keep their resolved primitive type.
		switch (expr.kind) {
		case ast::Expr::Kind::Integer: {
			const auto &integer = static_cast<const ast::IntegerExpr &>(expr);
			ir::Type type = expected && is_integer(*expected) ? *expected : i32_type();
			return std::make_unique<ir::IntegerValue>(type, integer.literal, false);
		}
		case ast::Expr::Kind::Bool: {
			const auto &boolean = static_cast<const ast::BoolExpr &>(expr);
			return std::make_unique<ir::BoolValue>(boolean.value);
		}
		case ast::Expr::Kind::Char: {
			const auto &character = static_cast<const ast::CharExpr &>(expr);
			return std::make_unique<ir::CharValue>(character.value);
		}
		case ast::Expr::Kind::String: {
			const auto &string = static_cast<const ast::StringExpr &>(expr);
			return std::make_unique<ir::StringValue>(string.value);
		}
		case ast::Expr::Kind::Name: {
			const auto &name = static_cast<const ast::NameExpr &>(expr);
			auto it = locals.find(name.name);
			if (it != locals.end())
				return std::make_unique<ir::LocalValue>(name.name, it->second);
			auto global = resolve_global(name.name);
			if (global != nullptr)
				return std::make_unique<ir::GlobalValue>(global->symbol_name, global->type);
			throw std::runtime_error("unknown name in IR lowering: " + name.name);
		}
		case ast::Expr::Kind::Binary: {
			const auto &binary = static_cast<const ast::BinaryExpr &>(expr);
			if (is_logical_operator(binary.op)) {
				return std::make_unique<ir::BinaryValue>(
					binary.op, lower_expr(*binary.lhs, locals, bool_type()),
					lower_expr(*binary.rhs, locals, bool_type()), bool_type());
			}

			auto lhs = lower_expr(*binary.lhs, locals, expected);
			ir::Type operand_type = lhs->type;
			auto rhs = lower_expr(*binary.rhs, locals, operand_type);
			ir::Type type =
				is_comparison_operator(binary.op) ? bool_type()
				                                  : lower_binary_result_type(binary.op, operand_type);
			return std::make_unique<ir::BinaryValue>(binary.op, std::move(lhs),
			                                         std::move(rhs), type);
		}
		case ast::Expr::Kind::Cast: {
			const auto &cast = static_cast<const ast::CastExpr &>(expr);
			return std::make_unique<ir::CastValue>(
				lower_expr(*cast.value, locals), lower_type(cast.target));
		}
		case ast::Expr::Kind::Unary:
			return lower_unary(static_cast<const ast::UnaryExpr &>(expr), locals, expected);
		case ast::Expr::Kind::Call: {
			const auto &call_expr = static_cast<const ast::CallExpr &>(expr);
			if (call_expr.callee_path.size() == 1 && call_expr.callee_path[0] == "len")
				return lower_len_call(call_expr, locals);
			auto function = resolve_function(call_expr.callee_path);
			if (function == nullptr) {
				if (auto variant = resolve_enum_variant(call_expr.callee_path, expected))
					return lower_enum_literal(call_expr, *variant, locals);
				throw std::runtime_error("unknown function in IR lowering: " +
				                         call_expr.callee);
			}

			// FE-103: a call to a generic template lowers via monomorphization.
			// We lower each arg first so the unifier sees concrete types,
			// then mangle a fresh symbol per (template, bindings) pair and
			// queue the instantiation if not yet emitted.
			if (!function->generic_parameters.empty()) {
				std::vector<std::unique_ptr<ir::Value>> lowered_args;
				lowered_args.reserve(call_expr.arguments.size());
				for (const auto &arg : call_expr.arguments)
					lowered_args.push_back(lower_expr(*arg, locals));
				std::unordered_map<std::string, ir::Type> bindings;
				if (!infer_call_bindings(*function, lowered_args, bindings))
					throw std::runtime_error(
					    "failed to infer generic types for call to '" +
					    call_expr.callee + "' in IR lowering");
				std::string mangled =
				    function->symbol_name +
				    mangle_generic_suffix(function->generic_parameters, bindings);
				if (monomorph_done_.insert(mangled).second && function->ast_function) {
					PendingMonomorph pending;
					pending.ast_function = function->ast_function;
					pending.mangled_name = mangled;
					pending.module_path = function->ast_function->module_path;
					pending.bindings = bindings;
					monomorph_queue_.push_back(std::move(pending));
				}
				// FE-103.1: substitute_generics may produce an angle-bracket
				// form like `Box<i32>` (when the function's return type was
				// kept unmangled because args contained a type variable).
				// Re-route through lower_type_name to mangle and register
				// the monomorph layout, ensuring the CallValue's type has
				// proper bits for downstream chunk computation.
				ir::Type instantiated_return =
				    substitute_generics(function->return_type, bindings);
				if (is_user_struct(instantiated_return) &&
				    instantiated_return.name.find('<') != std::string::npos) {
					instantiated_return = lower_type_name(instantiated_return.name);
				}
				auto call = std::make_unique<ir::CallValue>(
				    mangled, instantiated_return);
				for (auto &arg : lowered_args)
					call->arguments.push_back(std::move(arg));
				return call;
			}

			auto call = std::make_unique<ir::CallValue>(function->symbol_name,
			                                            function->return_type);
			for (std::size_t i = 0; i < call_expr.arguments.size(); ++i) {
				std::optional<ir::Type> parameter_type;
				if (i < function->parameter_types.size())
					parameter_type = function->parameter_types[i];
				call->arguments.push_back(
					lower_expr(*call_expr.arguments[i], locals, parameter_type));
			}
			return call;
		}
		case ast::Expr::Kind::StructLiteral: {
			const auto &literal = static_cast<const ast::StructLiteralExpr &>(expr);
			return lower_struct_literal(literal, locals, expected);
		}
		case ast::Expr::Kind::Tuple: {
			const auto &tuple = static_cast<const ast::TupleExpr &>(expr);
			return lower_tuple_literal(tuple, locals, expected);
		}
		case ast::Expr::Kind::FieldAccess: {
			const auto &access = static_cast<const ast::FieldAccessExpr &>(expr);
			return lower_field_access(access, locals);
		}
		case ast::Expr::Kind::Index: {
			const auto &index = static_cast<const ast::IndexExpr &>(expr);
			return lower_index_expr(index, locals);
		}
		case ast::Expr::Kind::Try: {
			const auto &try_expr = static_cast<const ast::TryExpr &>(expr);
			return lower_try_expr(try_expr, locals);
		}
		}

		throw std::runtime_error("unexpected expression in IR lowering");
	}

	std::unique_ptr<ir::Value> lower_try_expr(const ast::TryExpr &try_expr,
	                                          const Locals &locals)
	{
		// FE-012: lower `expr?` as
		//   let __try_tmp_N = expr;
		//   if (result_i32_is_err(__try_tmp_N)) { return __try_tmp_N; }
		//   <result_i32_value_or(__try_tmp_N, 0)>   // value of the expression
		//
		// The pre-statements are emitted into pending_pre_statements_, which
		// the surrounding statement loop flushes before the consuming
		// statement. Per FE-012's Phase 1 scoping note, this codepath only
		// supports Result<i32> with the function returning Result<i32>; the
		// generic case is gated on FE-103.
		auto operand = lower_expr(*try_expr.operand, locals);
		if (!is_result(operand->type))
			throw std::runtime_error("'?' operator on non-Result in IR lowering");

		auto payload = handle_payload_type(operand->type).value_or(i32_type());
		if (operand->type != result_of(i32_type()) || payload != i32_type())
			throw std::runtime_error(
			    "'?' operator only supports Result<i32> in IR lowering "
			    "until FE-103 monomorphization lands");

		if (!pending_pre_statements_)
			throw std::runtime_error(
			    "'?' operator used outside a statement context in IR lowering");

		std::string tmp_name = "__try_tmp_" + std::to_string(try_temp_counter_++);
		ir::Type result_type = operand->type;

		// let __try_tmp_N = <operand>;
		pending_pre_statements_->push_back(std::make_unique<ir::LetStatement>(
		    tmp_name, std::move(operand)));

		// if (result_i32_is_err(__try_tmp_N)) { return __try_tmp_N; }
		auto is_err_call =
		    std::make_unique<ir::CallValue>("result_i32_is_err", bool_type());
		is_err_call->arguments.push_back(
		    std::make_unique<ir::LocalValue>(tmp_name, result_type));

		std::vector<std::unique_ptr<ir::Statement>> then_body;
		then_body.push_back(std::make_unique<ir::ReturnStatement>(
		    std::make_unique<ir::LocalValue>(tmp_name, result_type)));
		pending_pre_statements_->push_back(std::make_unique<ir::IfStatement>(
		    std::move(is_err_call), std::move(then_body),
		    std::vector<std::unique_ptr<ir::Statement>>{}));

		// result_i32_value_or(__try_tmp_N, 0) — the propagated Ok value
		auto value_or_call =
		    std::make_unique<ir::CallValue>("result_i32_value_or", payload);
		value_or_call->arguments.push_back(
		    std::make_unique<ir::LocalValue>(tmp_name, result_type));
		value_or_call->arguments.push_back(
		    std::make_unique<ir::IntegerValue>(payload, "0", false));
		return value_or_call;
	}

	std::unique_ptr<ir::Value> lower_len_call(
		const ast::CallExpr &call, const Locals &locals)
	{
		if (call.arguments.size() != 1)
			throw std::runtime_error("invalid len() call in IR lowering");
		auto argument = lower_expr(*call.arguments[0], locals);
		if (argument->type.kind == PrimitiveKind::Str) {
			auto lowered = std::make_unique<ir::CallValue>("strlen", i32_type());
			lowered->arguments.push_back(std::move(argument));
			return lowered;
		}
		if (!is_slice(argument->type))
			throw std::runtime_error("len() on non-slice in IR lowering");
		auto element = handle_payload_type(argument->type);
		auto suffix = element ? slice_helper_type_suffix(*element) : std::nullopt;
		if (!suffix)
			throw std::runtime_error("unsupported slice len element type in IR lowering");
		auto lowered = std::make_unique<ir::CallValue>("slice_" + *suffix + "_len", i32_type());
		lowered->arguments.push_back(std::move(argument));
		return lowered;
	}

	std::unique_ptr<ir::Value> lower_index_expr(
		const ast::IndexExpr &index, const Locals &locals)
	{
		auto base = lower_expr(*index.base, locals);
		auto offset = lower_expr(*index.index, locals, i32_type());
		if (is_slice(base->type)) {
			auto element = handle_payload_type(base->type);
			auto suffix = element ? slice_helper_type_suffix(*element) : std::nullopt;
			if (!element || !suffix)
				throw std::runtime_error("unsupported slice index element type in IR lowering");
			auto lowered = std::make_unique<ir::CallValue>("slice_" + *suffix + "_at", *element);
			lowered->arguments.push_back(std::move(base));
			lowered->arguments.push_back(std::move(offset));
			return lowered;
		}

		ir::Type result_type = u8_type();
		ir::Type address_type = pointer_to(u8_type());
		if (base->type.kind != PrimitiveKind::Str) {
			auto pointee = pointee_type(base->type);
			if (!pointee)
				throw std::runtime_error("index base is not pointer, str, or slice in IR lowering");
			result_type = *pointee;
			address_type = base->type;
		}
		auto address = std::make_unique<ir::BinaryValue>(
			"+", std::move(base), std::move(offset), address_type);
		return std::make_unique<ir::UnaryValue>("*", std::move(address), result_type);
	}

	std::unique_ptr<ir::Value> lower_struct_literal(
		const ast::StructLiteralExpr &literal, const Locals &locals,
		std::optional<ir::Type> expected = std::nullopt)
	{
		ir::Type literal_type = lower_type(literal.type);
		// FE-103: when the literal's bare type is a generic template and
		// the expected type is one of its monomorphs, adopt the monomorph
		// so field offsets and types come from the substituted layout.
		if (expected && is_user_struct(literal_type) && is_user_struct(*expected) &&
		    generic_struct_templates_.count(literal_type.name) > 0)
			literal_type = *expected;
		auto layout_it = struct_layouts_.find(literal_type.name);
		if (layout_it == struct_layouts_.end())
			throw std::runtime_error("unknown struct in IR lowering: " + literal_type.name);
		auto lowered = std::make_unique<ir::StructLiteralValue>(
			literal_type, layout_it->second.total_size);
		for (const auto &field : literal.fields) {
			auto field_layout = layout_it->second.field(field.name);
			if (!field_layout)
				throw std::runtime_error("unknown field in IR lowering: " + field.name);
			lowered->fields.push_back(ir::StructLiteralValue::Field{
			    field.name, field_layout->type, field_layout->offset,
			    lower_expr(*field.value, locals, field_layout->type)});
		}
		return lowered;
	}

	std::unique_ptr<ir::Value> lower_tuple_literal(
		const ast::TupleExpr &tuple, const Locals &locals,
		std::optional<ir::Type> expected)
	{
		std::optional<std::vector<PrimitiveType>> expected_elements;
		if (expected && is_tuple(*expected))
			expected_elements = tuple_elements(*expected);

		std::vector<PrimitiveType> element_types;
		element_types.reserve(tuple.elements.size());
		std::vector<std::unique_ptr<ir::Value>> values;
		values.reserve(tuple.elements.size());
		for (std::size_t i = 0; i < tuple.elements.size(); ++i) {
			std::optional<ir::Type> element_expected;
			if (expected_elements && i < expected_elements->size())
				element_expected = (*expected_elements)[i];
			auto value = lower_expr(*tuple.elements[i], locals, element_expected);
			element_types.push_back(value->type);
			values.push_back(std::move(value));
		}

		ir::Type literal_type = tuple_type(std::move(element_types));
		TupleLayout layout = layout_tuple_elements(*literal_type.elements);
		auto lowered = std::make_unique<ir::StructLiteralValue>(
			literal_type, layout.total_size);
		for (std::size_t i = 0; i < values.size(); ++i) {
			lowered->fields.push_back(ir::StructLiteralValue::Field{
			    std::to_string(i), layout.elements[i].type, layout.elements[i].offset,
			    std::move(values[i])});
		}
		return lowered;
	}

	std::optional<EnumVariantLowerInfo> resolve_enum_variant(
		const std::vector<std::string> &path, std::optional<ir::Type> expected) const
	{
		if (path.size() == 2) {
			auto it = enum_variants_by_qualified_.find(path[0] + "::" + path[1]);
			if (it != enum_variants_by_qualified_.end())
				return it->second;
			return std::nullopt;
		}
		if (path.size() != 1)
			return std::nullopt;

		if (expected && is_user_enum(*expected)) {
			auto it = enum_variants_by_qualified_.find(expected->name + "::" + path[0]);
			if (it != enum_variants_by_qualified_.end())
				return it->second;
		}

		auto matches = enum_variants_by_name_.find(path[0]);
		if (matches == enum_variants_by_name_.end() || matches->second.size() != 1)
			return std::nullopt;
		return matches->second.front();
	}

	std::unique_ptr<ir::Value> lower_enum_literal(
		const ast::CallExpr &call, const EnumVariantLowerInfo &variant,
		const Locals &locals)
	{
		auto lowered = std::make_unique<ir::EnumLiteralValue>(
			variant.enum_type, variant.variant.tag, variant.total_size);
		for (std::size_t i = 0; i < call.arguments.size(); ++i) {
			const auto &field = variant.variant.fields[i];
			lowered->payloads.push_back(ir::EnumLiteralValue::Payload{
				field.type,
				variant.payload_offset + field.offset,
				lower_expr(*call.arguments[i], locals, field.type)});
		}
		return lowered;
	}

	std::unique_ptr<ir::Value> lower_field_access(
		const ast::FieldAccessExpr &access, const Locals &locals)
	{
		if (access.base->kind != ast::Expr::Kind::Unary)
			return lower_struct_value_field_access(access, locals);

		const auto &deref = static_cast<const ast::UnaryExpr &>(*access.base);
		if (deref.op != "*")
			return lower_struct_value_field_access(access, locals);

		auto pointer_value = lower_expr(*deref.operand, locals);
		if (!is_pointer(pointer_value->type))
			throw std::runtime_error("field access requires pointer-to-struct base");
		auto pointee = pointee_type(pointer_value->type);
		if (!pointee || !is_user_struct(*pointee))
			throw std::runtime_error("field access requires pointer-to-struct base");

		auto layout_it = struct_layouts_.find(pointee->name);
		if (layout_it == struct_layouts_.end())
			throw std::runtime_error("unknown struct in IR lowering: " + pointee->name);
		auto field_layout = layout_it->second.field(access.field);
		if (!field_layout)
			throw std::runtime_error("unknown field in IR lowering: " + access.field);

		ir::Type field_type = field_layout->type;
		auto offset_value = std::make_unique<ir::IntegerValue>(
			i32_type(), std::to_string(field_layout->offset), false);

		// Cast pointer-to-struct to pointer-to-u8 so '+' arithmetic advances
		// by exactly `offset` bytes (pointer arithmetic scales by pointee
		// size; *u8 has size 1).
		auto byte_ptr = std::make_unique<ir::CastValue>(
			std::move(pointer_value), pointer_to(u8_type()));
		auto field_byte_ptr = std::make_unique<ir::BinaryValue>(
			"+", std::move(byte_ptr), std::move(offset_value),
			pointer_to(u8_type()));
		auto field_typed_ptr = std::make_unique<ir::CastValue>(
			std::move(field_byte_ptr), pointer_to(field_type));
		return std::make_unique<ir::UnaryValue>(
			"*", std::move(field_typed_ptr), field_type);
	}

	std::unique_ptr<ir::Value> lower_struct_value_field_access(
		const ast::FieldAccessExpr &access, const Locals &locals)
	{
		auto base = lower_expr(*access.base, locals);
		if (is_tuple(base->type)) {
			auto elements = tuple_elements(base->type);
			auto index = parse_tuple_field_index(access.field);
			if (!elements || !index || *index >= elements->size())
				throw std::runtime_error("unknown tuple field in IR lowering: " +
				                         access.field);
			TupleLayout layout = layout_tuple_elements(*elements);
			return std::make_unique<ir::StructFieldValue>(
				std::move(base), layout.elements[*index].type,
				layout.elements[*index].offset);
		}
		if (!is_user_struct(base->type))
			throw std::runtime_error("field access requires struct value");
		auto layout_it = struct_layouts_.find(base->type.name);
		if (layout_it == struct_layouts_.end())
			throw std::runtime_error("unknown struct in IR lowering: " + base->type.name);
		auto field_layout = layout_it->second.field(access.field);
		if (!field_layout)
			throw std::runtime_error("unknown field in IR lowering: " + access.field);
		return std::make_unique<ir::StructFieldValue>(
			std::move(base), field_layout->type, field_layout->offset);
	}

	std::unique_ptr<ir::Value> lower_unary(
		const ast::UnaryExpr &unary, const Locals &locals,
		std::optional<ir::Type> expected)
	{
		auto operand = lower_expr(*unary.operand, locals,
		                          unary.op == "*" ? std::nullopt : expected);
		ir::Type type = operand->type;
		if (unary.op == "&") {
			type = pointer_to(type);
		} else if (unary.op == "*") {
			auto target_type = pointee_type(type);
			if (!target_type)
				throw std::runtime_error("dereference of non-pointer in IR lowering");
			type = *target_type;
		}
		return std::make_unique<ir::UnaryValue>(unary.op, std::move(operand), type);
	}

	ir::Type lower_binary_result_type(const std::string &op, ir::Type lhs_type) const
	{
		if (lhs_type.kind == PrimitiveKind::Str && (op == "+" || op == "-"))
			return pointer_to(u8_type());
		return lhs_type;
	}

	ir::MatchPattern lower_match_pattern(const ast::MatchPattern &pattern,
	                                     ir::Type value_type) const
	{
		ir::MatchPattern lowered;
		lowered.type = value_type;
		switch (pattern.kind) {
		case ast::MatchPattern::Kind::Default:
			lowered.kind = ir::MatchPattern::Kind::Default;
			break;
		case ast::MatchPattern::Kind::Integer:
			lowered.kind = ir::MatchPattern::Kind::Integer;
			lowered.literal = pattern.literal;
			lowered.is_negative = pattern.is_negative;
			break;
		case ast::MatchPattern::Kind::Bool:
			lowered.kind = ir::MatchPattern::Kind::Bool;
			lowered.bool_value = pattern.bool_value;
			break;
		case ast::MatchPattern::Kind::Char:
			lowered.kind = ir::MatchPattern::Kind::Char;
			lowered.char_value = pattern.char_value;
			break;
		case ast::MatchPattern::Kind::Variant: {
			auto variant = resolve_enum_variant(pattern.path, value_type);
			if (!variant)
				throw std::runtime_error("unknown enum variant in IR lowering");
			lowered.kind = ir::MatchPattern::Kind::EnumVariant;
			lowered.type = variant->enum_type;
			lowered.tag = variant->variant.tag;
			for (std::size_t i = 0; i < pattern.bindings.size(); ++i) {
				if (pattern.bindings[i] == "_")
					continue;
				const auto &field = variant->variant.fields[i];
				lowered.bindings.push_back(ir::MatchPattern::Binding{
				    pattern.bindings[i], field.type,
				    variant->payload_offset + field.offset});
			}
			break;
		}
		case ast::MatchPattern::Kind::Struct: {
			if (pattern.path.empty())
				throw std::runtime_error("empty struct pattern in IR lowering");
			auto layout_it = struct_layouts_.find(pattern.path[0]);
			if (layout_it == struct_layouts_.end())
				throw std::runtime_error("unknown struct pattern in IR lowering: " +
				                         pattern.path[0]);
			lowered.kind = ir::MatchPattern::Kind::Struct;
			for (std::size_t i = 0; i < pattern.bindings.size(); ++i) {
				if (pattern.bindings[i] == "_")
					continue;
				const auto &field = layout_it->second.fields[i];
				lowered.bindings.push_back(ir::MatchPattern::Binding{
				    pattern.bindings[i], field.type, field.offset});
			}
			break;
		}
		}
		return lowered;
	}

	ir::MatchArm lower_match_arm(const ast::MatchArm &arm, ir::Type value_type,
	                             ir::Type function_return_type, const Locals &locals)
	{
		std::vector<ir::MatchPattern> patterns;
		Locals arm_locals = locals;
		for (const auto &pattern : arm.patterns) {
			patterns.push_back(lower_match_pattern(pattern, value_type));
			for (const auto &binding : patterns.back().bindings)
				arm_locals[binding.name] = binding.type;
		}
		return ir::MatchArm{
		    std::move(patterns),
		    lower_statements(arm.body, function_return_type, arm_locals),
		};
	}

	std::vector<std::unique_ptr<ir::Statement>> lower_statements(
		const std::vector<std::unique_ptr<ast::Stmt>> &statements,
		ir::Type function_return_type, Locals locals)
	{
		std::vector<std::unique_ptr<ir::Statement>> lowered;
		for (const auto &statement : statements) {
			// FE-013: `unsafe { ... }` is purely a sema concept; the body
			// inlines into the surrounding statement list at IR level.
			if (statement->kind == ast::Stmt::Kind::UnsafeBlock) {
				const auto &block =
				    static_cast<const ast::UnsafeBlockStmt &>(*statement);
				auto inner =
				    lower_statements(block.body, function_return_type, locals);
				for (auto &s : inner)
					lowered.push_back(std::move(s));
				continue;
			}
			std::vector<std::unique_ptr<ir::Statement>> pre_statements;
			auto *previous_pre = pending_pre_statements_;
			pending_pre_statements_ = &pre_statements;
			auto stmt = lower_statement(*statement, function_return_type, locals);
			pending_pre_statements_ = previous_pre;
			for (auto &pre : pre_statements)
				lowered.push_back(std::move(pre));
			lowered.push_back(std::move(stmt));
		}
		return lowered;
	}

	std::unique_ptr<ir::Statement> lower_statement(const ast::Stmt &statement,
	                                               ir::Type function_return_type,
	                                               Locals &locals)
	{
		if (statement.kind == ast::Stmt::Kind::Let) {
			const auto &let = static_cast<const ast::LetStmt &>(statement);
			ir::Type let_type = lower_type(let.type);
			auto initializer = lower_expr(*let.initializer, locals, let_type);
			locals[let.name] = let_type;
			return std::make_unique<ir::LetStatement>(let.name, std::move(initializer));
		}

		if (statement.kind == ast::Stmt::Kind::Assign) {
			const auto &assign = static_cast<const ast::AssignStmt &>(statement);
			auto it = locals.find(assign.name);
			if (it == locals.end()) {
				auto global = resolve_global(assign.name);
				if (global == nullptr)
					throw std::runtime_error("unknown name in IR lowering: " + assign.name);
				return std::make_unique<ir::AssignStatement>(
					global->symbol_name, lower_expr(*assign.value, locals, global->type));
			}
			return std::make_unique<ir::AssignStatement>(
				assign.name, lower_expr(*assign.value, locals, it->second));
		}

		if (statement.kind == ast::Stmt::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ast::IndirectAssignStmt &>(statement);
			auto target = lower_expr(*assign.target, locals);
			auto target_pointee = pointee_type(target->type);
			if (!target_pointee)
				throw std::runtime_error("indirect assignment through non-pointer in IR lowering");
			return std::make_unique<ir::IndirectAssignStatement>(
				std::move(target), lower_expr(*assign.value, locals, *target_pointee));
		}

		if (statement.kind == ast::Stmt::Kind::FieldAssign) {
			const auto &assign = static_cast<const ast::FieldAssignStmt &>(statement);
			// Lower (*p).field = value to *(((p as *u8) + offset) as *FieldType) = value
			auto pointer_value = lower_expr(*assign.base, locals);
			auto pointee = pointee_type(pointer_value->type);
			if (!pointee || !is_user_struct(*pointee))
				throw std::runtime_error("field assignment on non-struct pointer in IR lowering");

			auto layout_it = struct_layouts_.find(pointee->name);
			if (layout_it == struct_layouts_.end())
				throw std::runtime_error("unknown struct in IR lowering: " + pointee->name);
			auto field_layout = layout_it->second.field(assign.field);
			if (!field_layout)
				throw std::runtime_error("unknown field in IR lowering: " + assign.field);

			ir::Type field_type = field_layout->type;
			auto offset_value = std::make_unique<ir::IntegerValue>(
				i32_type(), std::to_string(field_layout->offset), false);
			auto byte_ptr = std::make_unique<ir::CastValue>(
				std::move(pointer_value), pointer_to(u8_type()));
			auto field_byte_ptr = std::make_unique<ir::BinaryValue>(
				"+", std::move(byte_ptr), std::move(offset_value),
				pointer_to(u8_type()));
			auto field_typed_ptr = std::make_unique<ir::CastValue>(
				std::move(field_byte_ptr), pointer_to(field_type));
			return std::make_unique<ir::IndirectAssignStatement>(
				std::move(field_typed_ptr),
				lower_expr(*assign.value, locals, field_type));
		}

		if (statement.kind == ast::Stmt::Kind::If) {
			const auto &if_stmt = static_cast<const ast::IfStmt &>(statement);
			auto condition = lower_expr(*if_stmt.condition, locals, bool_type());
			return std::make_unique<ir::IfStatement>(
				std::move(condition),
				lower_statements(if_stmt.then_body, function_return_type, locals),
				lower_statements(if_stmt.else_body, function_return_type, locals));
		}

		if (statement.kind == ast::Stmt::Kind::Match) {
			const auto &match_stmt = static_cast<const ast::MatchStmt &>(statement);
			auto value = lower_expr(*match_stmt.value, locals);
			ir::Type value_type = value->type;
			std::vector<ir::MatchArm> arms;
			for (const auto &arm : match_stmt.arms)
				arms.push_back(lower_match_arm(arm, value_type, function_return_type,
				                               locals));
			return std::make_unique<ir::MatchStatement>(std::move(value), std::move(arms));
		}

		if (statement.kind == ast::Stmt::Kind::While) {
			const auto &while_stmt = static_cast<const ast::WhileStmt &>(statement);
			auto condition = lower_expr(*while_stmt.condition, locals, bool_type());
			return std::make_unique<ir::WhileStatement>(
				std::move(condition),
				lower_statements(while_stmt.body, function_return_type, locals));
		}

		if (statement.kind == ast::Stmt::Kind::For) {
			const auto &for_stmt = static_cast<const ast::ForStmt &>(statement);
			auto for_locals = locals;
			auto initializer =
				lower_statement(*for_stmt.initializer, function_return_type, for_locals);
			auto condition = lower_expr(*for_stmt.condition, for_locals, bool_type());
			auto increment =
				lower_statement(*for_stmt.increment, function_return_type, for_locals);
			return std::make_unique<ir::ForStatement>(
				std::move(initializer), std::move(condition), std::move(increment),
				lower_statements(for_stmt.body, function_return_type, for_locals));
		}

		if (statement.kind == ast::Stmt::Kind::Break)
			return std::make_unique<ir::BreakStatement>();

		if (statement.kind == ast::Stmt::Kind::Continue)
			return std::make_unique<ir::ContinueStatement>();

		if (statement.kind == ast::Stmt::Kind::Expr) {
			const auto &expr_statement = static_cast<const ast::ExprStmt &>(statement);
			return std::make_unique<ir::ExprStatement>(
				lower_expr(*expr_statement.value, locals));
		}

		const auto &ret = static_cast<const ast::ReturnStmt &>(statement);
		return std::make_unique<ir::ReturnStatement>(
			lower_expr(*ret.value, locals, function_return_type));
	}

	ir::Function lower_function(const ast::Function &function)
	{
		current_module_path_ = &function.module_path;
		ir::Function lowered;
		lowered.is_extern = function.is_extern;
		lowered.name = symbol_item_path(function.module_path, function.name);
		lowered.return_type = lower_type(function.return_type);

		Locals locals;
		for (const auto &parameter : function.parameters) {
			ir::Type parameter_type = lower_type(parameter.type);
			lowered.parameters.push_back({parameter.name, parameter_type});
			locals[parameter.name] = parameter_type;
		}

		lowered.body = lower_statements(function.body, lowered.return_type, locals);

		current_module_path_ = nullptr;
		return lowered;
	}

	// FE-103: instantiate one generic-function monomorph with bound types.
	// `pending` carries the AST template plus a substitution map keyed by
	// type-parameter name; current_type_substitutions_ is set for the
	// duration so lower_type resolves type variables.
	ir::Function lower_monomorph(const PendingMonomorph &pending)
	{
		auto saved_substitutions = current_type_substitutions_;
		current_type_substitutions_ = pending.bindings;
		current_module_path_ = &pending.module_path;
		ir::Function lowered;
		lowered.is_extern = pending.ast_function->is_extern;
		lowered.name = pending.mangled_name;
		lowered.return_type = lower_type(pending.ast_function->return_type);

		Locals locals;
		for (const auto &parameter : pending.ast_function->parameters) {
			ir::Type parameter_type = lower_type(parameter.type);
			lowered.parameters.push_back({parameter.name, parameter_type});
			locals[parameter.name] = parameter_type;
		}

		lowered.body =
		    lower_statements(pending.ast_function->body, lowered.return_type, locals);

		current_module_path_ = nullptr;
		current_type_substitutions_ = std::move(saved_substitutions);
		return lowered;
	}

	// FE-103: re-run unification at the call site to derive the type
	// bindings (sema already validated this; we trust). Returns true on
	// success and fills `bindings`.
	bool infer_call_bindings(const FunctionInfo &info,
	                         const std::vector<std::unique_ptr<ir::Value>> &args,
	                         std::unordered_map<std::string, ir::Type> &bindings)
	{
		if (info.parameter_types.size() != args.size())
			return false;
		std::unordered_set<std::string> generic_names(
		    info.generic_parameters.begin(), info.generic_parameters.end());
		for (std::size_t i = 0; i < args.size(); ++i) {
			if (!unify_generic_pattern(info.parameter_types[i], args[i]->type,
			                           generic_names, bindings))
				return false;
		}
		for (const auto &name : info.generic_parameters)
			if (bindings.find(name) == bindings.end())
				return false;
		return true;
	}

	ir::StaticBuffer lower_static_buffer(const ast::StaticBuffer &buffer)
	{
		ir::StaticBuffer lowered;
		lowered.name = symbol_item_path(buffer.module_path, buffer.name);
		lowered.element_type = lower_type(buffer.element_type);
		lowered.length = static_cast<std::size_t>(std::stoull(buffer.length_literal));
		for (const auto &initializer : buffer.initializers)
			lowered.initializers.push_back(lower_static_initializer(initializer));
		return lowered;
	}

	ir::StaticBuffer::Initializer lower_static_initializer(
		const ast::StaticBuffer::Initializer &initializer)
	{
		ir::StaticBuffer::Initializer lowered;
		switch (initializer.kind) {
		case ast::StaticBuffer::Initializer::Kind::Integer:
			lowered.kind = ir::StaticBuffer::Initializer::Kind::Integer;
			break;
		case ast::StaticBuffer::Initializer::Kind::Bool:
			lowered.kind = ir::StaticBuffer::Initializer::Kind::Bool;
			break;
		case ast::StaticBuffer::Initializer::Kind::Char:
			lowered.kind = ir::StaticBuffer::Initializer::Kind::Char;
			break;
		case ast::StaticBuffer::Initializer::Kind::String:
			lowered.kind = ir::StaticBuffer::Initializer::Kind::String;
			break;
		}
		lowered.literal = initializer.literal;
		lowered.bool_value = initializer.bool_value;
		lowered.char_value = initializer.char_value;
		lowered.is_negative = initializer.is_negative;
		return lowered;
	}

	ir::StaticScalar lower_static_scalar(const ast::StaticScalar &scalar)
	{
		ir::StaticScalar lowered;
		lowered.name = symbol_item_path(scalar.module_path, scalar.name);
		lowered.type = lower_type(scalar.type);
		lowered.initializer_literal = scalar.initializer_literal;
		return lowered;
	}

	const ast::Module &module_;
	LowerOptions options_;
	std::unordered_map<std::string, GlobalInfo> globals_;
	std::unordered_map<std::string, FunctionInfo> functions_;
	std::unordered_map<std::string, ImportScope> imports_;
	std::unordered_set<std::string> modules_;
	std::unordered_map<std::string, StructLayout> struct_layouts_;
	// FE-103: generic struct templates indexed by base name; each
	// `Box<i32>`-style instantiation is materialized lazily into
	// struct_layouts_ under its mangled name.
	std::unordered_map<std::string, const ast::StructDecl *> generic_struct_templates_;
	std::unordered_map<std::string, EnumLayoutInfo> enum_layouts_;
	std::unordered_map<std::string, EnumVariantLowerInfo> enum_variants_by_qualified_;
	std::unordered_map<std::string, std::vector<EnumVariantLowerInfo>> enum_variants_by_name_;
	const std::vector<std::string> *current_module_path_ = nullptr;
	std::vector<std::unique_ptr<ir::Statement>> *pending_pre_statements_ = nullptr;
	std::size_t try_temp_counter_ = 0;
	// FE-103: type-variable substitutions active while lowering a generic
	// monomorph (e.g. {"T" -> i32}). lower_type_name consults this first.
	std::unordered_map<std::string, ir::Type> current_type_substitutions_;
	std::vector<PendingMonomorph> monomorph_queue_;
	std::unordered_set<std::string> monomorph_done_;
};

} // namespace

ir::Module lower_to_ir(const ast::Module &module, LowerOptions options)
{
	return Lowerer(module, options).run();
}

} // namespace rexc
