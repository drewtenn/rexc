// Semantic analysis for the parsed Rexy AST.
//
// The parser proves only that source text has the shape of the grammar. This
// stage proves the program is meaningful enough to lower: functions and locals
// are declared once, names resolve, calls match signatures, expressions have
// compatible primitive types, integer literals fit their target types, and
// break/continue appear only inside loops.
#include "rexc/sema.hpp"
#include "rexc/stdlib.hpp"
#include "rexc/types.hpp"

#include "names.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
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

std::optional<std::uint64_t> parse_decimal_magnitude(const std::string &literal)
{
	// Parse from the original token text so huge literals are checked here,
	// rather than being silently wrapped by parser-side numeric conversion.
	std::uint64_t value = 0;
	for (char digit : literal) {
		std::uint64_t next_digit = static_cast<std::uint64_t>(digit - '0');
		if (value > (UINT64_MAX - next_digit) / 10)
			return std::nullopt;
		value = value * 10 + next_digit;
	}
	return value;
}

std::uint64_t max_signed_magnitude(PrimitiveType type)
{
	if (type.bits == 64)
		return 9223372036854775807ULL;
	return (1ULL << (type.bits - 1)) - 1;
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

bool is_negative_integer_literal(const ast::Expr &expr)
{
	if (expr.kind != ast::Expr::Kind::Unary)
		return false;
	const auto &unary = static_cast<const ast::UnaryExpr &>(expr);
	return unary.op == "-" && unary.operand->kind == ast::Expr::Kind::Integer;
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
	SourceLocation location;
	PrimitiveType return_type = PrimitiveType{PrimitiveKind::SignedInteger, 32};
	std::vector<PrimitiveType> parameter_types;
	std::vector<std::string> module_path;
	std::vector<std::string> generic_parameters;
	ast::Visibility visibility = ast::Visibility::Private;
	bool is_extern = false;
};

struct LocalInfo {
	PrimitiveType type;
	bool is_mutable = false;
};

struct GlobalInfo {
	PrimitiveType type;
	bool is_mutable = false;
	std::vector<std::string> module_path;
	ast::Visibility visibility = ast::Visibility::Private;
};

struct ModuleInfo {
	SourceLocation location;
	std::vector<std::string> module_path;
	ast::Visibility visibility = ast::Visibility::Private;
};

struct StructInfo {
	SourceLocation location;
	std::vector<std::string> module_path;
	ast::Visibility visibility = ast::Visibility::Private;
	std::vector<std::pair<std::string, PrimitiveType>> fields;
	std::size_t total_size = 0;
	std::size_t alignment = 1;
	bool is_generic = false;
	std::vector<std::string> generic_parameters; // FE-103
	std::string template_name; // FE-103: for monomorphs, the original generic struct name
	std::optional<PrimitiveType> field_type(const std::string &name) const
	{
		for (const auto &field : fields)
			if (field.first == name)
				return field.second;
		return std::nullopt;
	}
};

struct EnumVariantInfo {
	SourceLocation location;
	std::string name;
	std::uint32_t tag = 0;
	std::vector<PrimitiveType> payload_types;
};

struct EnumInfo {
	SourceLocation location;
	std::string name;
	std::vector<std::string> module_path;
	ast::Visibility visibility = ast::Visibility::Private;
	EnumLayout layout;
	std::vector<EnumVariantInfo> variants;

	const EnumVariantInfo *variant(const std::string &name) const
	{
		for (const auto &entry : variants)
			if (entry.name == name)
				return &entry;
		return nullptr;
	}
};

struct EnumVariantRef {
	const EnumInfo *enum_info = nullptr;
	const EnumVariantInfo *variant = nullptr;
};

enum class ImportKind { Function, Global, Module, Invalid };

struct ImportInfo {
	ImportKind kind;
	std::string target_key;
	std::vector<std::string> target_path;
};

class Analyzer {
public:
	Analyzer(const ast::Module &module, Diagnostics &diagnostics, SemanticOptions options)
		: module_(module), diagnostics_(diagnostics), options_(options)
	{
	}

	bool run()
	{
		build_module_table();
		build_type_tables();
		build_static_table();
		build_function_table();
		build_import_table();

		for (const auto &function : module_.functions) {
			if (!function.is_extern)
				analyze_function(function);
		}

		return !diagnostics_.has_errors();
	}

private:
	using ImportScope = std::unordered_map<std::string, ImportInfo>;

	const std::vector<stdlib::FunctionDecl> &bare_stdlib_functions() const
	{
		if (options_.stdlib_symbols == StdlibSymbolPolicy::All)
			return stdlib::stdlib_functions();
		return stdlib::prelude_functions();
	}

	bool include_stdlib_symbols() const
	{
		return options_.stdlib_symbols != StdlibSymbolPolicy::None;
	}

	bool include_bare_stdlib_symbols() const
	{
		return options_.stdlib_symbols != StdlibSymbolPolicy::None;
	}

	void note_module_path(const std::vector<std::string> &module_path,
	                      ast::Visibility leaf_visibility = ast::Visibility::Private,
	                      bool public_prefixes = false)
	{
		for (std::size_t size = 1; size <= module_path.size(); ++size) {
			std::vector<std::string> prefix(module_path.begin(),
			                                module_path.begin() + size);
			ast::Visibility visibility =
			    (public_prefixes || size == module_path.size())
			        ? leaf_visibility
			        : ast::Visibility::Private;
			std::string key = canonical_path(prefix);
			auto [it, inserted] = modules_.emplace(
			    key, ModuleInfo{SourceLocation{}, prefix, visibility});
			if (inserted)
				continue;
			if (visibility == ast::Visibility::Public)
				it->second.visibility = ast::Visibility::Public;
		}
	}

	void note_item_path(const std::vector<std::string> &path, bool is_stdlib = false)
	{
		if (path.empty())
			return;
		std::vector<std::string> module_path(path.begin(), path.end() - 1);
		note_module_path(module_path,
		                 is_stdlib ? ast::Visibility::Public
		                           : ast::Visibility::Private,
		                 is_stdlib);
	}

	void reserve_stdlib_module_path(const std::vector<std::string> &path)
	{
		if (path.empty())
			return;
		for (std::size_t size = 1; size < path.size(); ++size) {
			std::vector<std::string> prefix(path.begin(), path.begin() + size);
			reserved_stdlib_module_paths_.insert(canonical_path(prefix));
		}
	}

	void reserve_stdlib_symbols()
	{
		for (const auto &symbol : stdlib::reserved_runtime_symbols())
			reserved_stdlib_function_symbols_.insert(symbol);
	}

	bool collides_with_reserved_stdlib_symbol(
	    const std::vector<std::string> &module_path, const std::string &name) const
	{
		return reserved_stdlib_function_symbols_.find(symbol_item_path(module_path, name)) !=
		       reserved_stdlib_function_symbols_.end();
	}

	void build_module_table()
	{
		if (include_stdlib_symbols()) {
			reserve_stdlib_symbols();
			for (const auto &function : stdlib::stdlib_functions()) {
				if (auto path = stdlib_path_for_symbol(function.name)) {
					reserve_stdlib_module_path(*path);
					note_item_path(*path, true);
				}
			}
		}

		for (const auto &module : module_.modules) {
			std::string key = canonical_path(module.module_path);
			auto existing = modules_.find(key);
			if (reserved_stdlib_module_paths_.find(key) !=
			        reserved_stdlib_module_paths_.end() ||
			    (existing != modules_.end() && !existing->second.location.file.empty())) {
				diagnostics_.error(module.location, "duplicate module '" + key + "'");
				continue;
			}
			note_module_path(module.module_path);
			modules_[key] = ModuleInfo{module.location, module.module_path,
			                           module.visibility};
		}
	}

	void build_type_tables()
	{
		// FE-105: register stdlib-declared structs (e.g. Arena) before user
		// structs so user code can name them in signatures and field
		// expressions. Field types arrive already resolved through
		// stdlib::resolve_source_type, so we skip check_type here.
		if (include_stdlib_symbols()) {
			for (const auto &decl : stdlib::stdlib_structs()) {
				if (structs_.find(decl.name) != structs_.end())
					continue;
				StructInfo info{SourceLocation{}, {}, ast::Visibility::Public,
				                {}, 0, 1};
				info.fields.reserve(decl.fields.size());
				for (const auto &field : decl.fields)
					info.fields.emplace_back(field.name, field.type);
				compute_struct_layout(info);
				structs_[decl.name] = std::move(info);
			}
		}

		// Multi-pass registration: first pass records all user type names so
		// structs and enums can cross-reference each other regardless of
		// declaration order. Later passes validate member/payload type names.
		for (const auto &decl : module_.structs) {
			if (structs_.find(decl.name) != structs_.end()) {
				diagnostics_.error(decl.location,
				                   "struct '" + decl.name + "' already declared");
				continue;
			}
			// FE-103: populate generic_parameters/is_generic during the
			// name-registration pass so that recursive field types like
			// `*Tree<T>` resolved during the field-walking pass below find
			// a real generic template instead of an empty placeholder.
			StructInfo info{decl.location, decl.module_path, decl.visibility,
			                {}, 0, 1};
			info.generic_parameters = decl.generic_parameters;
			info.is_generic = !decl.generic_parameters.empty();
			structs_[decl.name] = std::move(info);
		}

		for (const auto &decl : module_.enums) {
			if (enums_.find(decl.name) != enums_.end()) {
				diagnostics_.error(decl.location,
				                   "enum '" + decl.name + "' already declared");
				continue;
			}
			if (structs_.find(decl.name) != structs_.end()) {
				diagnostics_.error(decl.location,
				                   "type '" + decl.name + "' already declared");
				continue;
			}
			EnumInfo info;
			info.location = decl.location;
			info.name = decl.name;
			info.module_path = decl.module_path;
			info.visibility = decl.visibility;
			enums_[decl.name] = std::move(info);
		}

		for (const auto &decl : module_.structs) {
			auto it = structs_.find(decl.name);
			if (it == structs_.end())
				continue; // rejected as duplicate above
			// FE-102: register the struct's generic parameters so field
			// types like `T` resolve. Generic structs skip layout computation
			// — sizes are known only at FE-103 monomorphization.
			current_generic_parameters_.clear();
			for (const auto &name : decl.generic_parameters)
				current_generic_parameters_.insert(name);
			std::vector<std::pair<std::string, PrimitiveType>> fields;
			fields.reserve(decl.fields.size());
			for (const auto &field : decl.fields) {
				auto field_type = check_type(field.type);
				fields.emplace_back(field.name, field_type);
			}
			it->second.fields = std::move(fields);
			it->second.is_generic = !decl.generic_parameters.empty();
			it->second.generic_parameters = decl.generic_parameters; // FE-103
			if (!it->second.is_generic)
				compute_struct_layout(it->second);
			current_generic_parameters_.clear();
		}

		for (const auto &decl : module_.enums) {
			auto it = enums_.find(decl.name);
			if (it == enums_.end())
				continue; // rejected above

			std::unordered_set<std::string> seen_variants;
			std::vector<EnumVariantSpec> layout_specs;
			for (const auto &variant : decl.variants) {
				std::vector<PrimitiveType> payload_types;
				payload_types.reserve(variant.payload_types.size());
				for (const auto &payload_type : variant.payload_types)
					payload_types.push_back(check_type(payload_type));

				if (!seen_variants.insert(variant.name).second) {
					diagnostics_.error(
					    variant.location,
					    "variant '" + variant.name + "' already declared in enum '" +
					        decl.name + "'");
					continue;
				}

				layout_specs.push_back(
				    EnumVariantSpec{variant.name, payload_types});
				EnumVariantInfo info;
				info.location = variant.location;
				info.name = variant.name;
				info.payload_types = std::move(payload_types);
				it->second.variants.push_back(std::move(info));
			}

			it->second.layout = layout_enum_variants(layout_specs);
			for (std::size_t i = 0; i < it->second.variants.size() &&
			                        i < it->second.layout.variants.size(); ++i) {
				it->second.variants[i].tag = it->second.layout.variants[i].tag;
			}
		}
	}

	void build_static_table()
	{
		for (const auto &buffer : module_.static_buffers) {
			std::string key = canonical_item_path(buffer.module_path, buffer.name);
			std::string symbol = symbol_item_path(buffer.module_path, buffer.name);
			bool duplicate_global = globals_.find(key) != globals_.end();
			if (duplicate_global ||
			    collides_with_reserved_stdlib_symbol(buffer.module_path, buffer.name)) {
				diagnostics_.error(buffer.location,
				                   "duplicate static '" + (duplicate_global ? key : symbol) +
				                       "'");
				continue;
			}
			note_module_path(buffer.module_path);

			PrimitiveType element_type = check_type(buffer.element_type);
			if (!buffer.is_mutable && buffer.initializers.empty())
				diagnostics_.error(buffer.location, "static buffer must be mutable");

			auto length = parse_decimal_magnitude(buffer.length_literal);
			if (!length || *length == 0)
				diagnostics_.error(buffer.location, "static buffer length must be greater than zero");
			if (length && buffer.initializers.size() > *length) {
				diagnostics_.error(buffer.location,
				                   "static array has more initializers than its length");
			}
			check_static_array_initializers(buffer, element_type);

			PrimitiveType global_type =
			    element_type == u8_type() ? PrimitiveType{PrimitiveKind::Str}
			                              : pointer_to(element_type);
			globals_[key] = GlobalInfo{global_type,
			                           buffer.is_mutable, buffer.module_path,
			                           buffer.visibility};
		}

		for (const auto &scalar : module_.static_scalars) {
			std::string key = canonical_item_path(scalar.module_path, scalar.name);
			std::string symbol = symbol_item_path(scalar.module_path, scalar.name);
			bool duplicate_global = globals_.find(key) != globals_.end();
			if (duplicate_global ||
			    collides_with_reserved_stdlib_symbol(scalar.module_path, scalar.name)) {
				diagnostics_.error(scalar.location,
				                   "duplicate static '" + (duplicate_global ? key : symbol) +
				                       "'");
				continue;
			}
			note_module_path(scalar.module_path);

			PrimitiveType type = check_type(scalar.type);
			if (type != i32_type()) {
				diagnostics_.error(scalar.type.location,
				                   "static scalars currently require i32 type");
			}
			if (!scalar.is_mutable)
				diagnostics_.error(scalar.location, "static scalar must be mutable");

			auto initializer = parse_decimal_magnitude(scalar.initializer_literal);
			if (!initializer || !integer_literal_fits(type, static_cast<std::int64_t>(*initializer))) {
				diagnostics_.error(scalar.location,
				                   "static scalar initializer does not fit in i32");
			}

			globals_[key] = GlobalInfo{type, scalar.is_mutable, scalar.module_path,
			                           scalar.visibility};
		}
	}

	void check_static_array_initializers(const ast::StaticBuffer &buffer,
	                                     PrimitiveType element_type)
	{
		for (const auto &initializer : buffer.initializers) {
			switch (initializer.kind) {
			case ast::StaticBuffer::Initializer::Kind::Integer:
				if (!is_integer(element_type)) {
					diagnostics_.error(initializer.location,
					                   "static array initializer type mismatch: expected '" +
					                       format_type(element_type) + "' but got 'i32'");
					continue;
				}
				check_integer_literal(initializer.location, element_type,
				                      initializer.literal, initializer.is_negative);
				break;
			case ast::StaticBuffer::Initializer::Kind::Bool:
				if (element_type != bool_type()) {
					diagnostics_.error(initializer.location,
					                   "static array initializer type mismatch: expected '" +
					                       format_type(element_type) + "' but got 'bool'");
				}
				break;
			case ast::StaticBuffer::Initializer::Kind::Char:
				if (element_type.kind != PrimitiveKind::Char) {
					diagnostics_.error(initializer.location,
					                   "static array initializer type mismatch: expected '" +
					                       format_type(element_type) + "' but got 'char'");
				}
				break;
			case ast::StaticBuffer::Initializer::Kind::String:
				if (element_type.kind != PrimitiveKind::Str) {
					diagnostics_.error(initializer.location,
					                   "static array initializer type mismatch: expected '" +
					                       format_type(element_type) + "' but got 'str'");
				}
				break;
			}
		}
	}

	void build_function_table()
	{
		if (include_stdlib_symbols()) {
			for (const auto &function : stdlib::stdlib_functions()) {
				FunctionInfo info{SourceLocation{}, function.return_type, function.parameters};
				info.visibility = ast::Visibility::Public;
				if (auto path = stdlib_path_for_symbol(function.name)) {
					info.module_path = std::vector<std::string>(path->begin(), path->end() - 1);
					functions_[canonical_path(*path)] = info;
				}
			}
			if (include_bare_stdlib_symbols()) {
				for (const auto &function : bare_stdlib_functions()) {
					FunctionInfo info{SourceLocation{}, function.return_type, function.parameters};
					info.visibility = ast::Visibility::Public;
					functions_[function.name] = std::move(info);
				}
			}
		}

		for (const auto &function : module_.functions) {
			std::string key = canonical_item_path(function.module_path, function.name);
			std::string symbol = symbol_item_path(function.module_path, function.name);
			bool duplicate_item =
			    functions_.find(key) != functions_.end() || globals_.find(key) != globals_.end();
			if (duplicate_item ||
			    collides_with_reserved_stdlib_symbol(function.module_path,
			                                         function.name)) {
				diagnostics_.error(
				    function.location,
				    "duplicate function '" + (duplicate_item ? key : symbol) + "'");
				continue;
			}
			note_module_path(function.module_path);
			// FE-102: register the function's generic parameters so the
			// signature's references to `T` resolve here too. Cleared after
			// each entry so the scope is per-function.
			current_generic_parameters_.clear();
			for (const auto &name : function.generic_parameters)
				current_generic_parameters_.insert(name);
			FunctionInfo info;
			info.location = function.location;
			info.return_type = check_type(function.return_type);
			for (const auto &parameter : function.parameters)
				info.parameter_types.push_back(check_type(parameter.type));
			info.module_path = function.module_path;
			info.visibility = function.visibility;
			info.is_extern = function.is_extern;
			info.generic_parameters = function.generic_parameters;
			functions_[key] = std::move(info);
			current_generic_parameters_.clear();
		}
	}

	void build_import_table()
	{
		for (const auto &use : module_.uses) {
			if (use.import_path.empty())
				continue;

			std::string target_key = canonical_path(use.import_path);
			std::string alias = use.import_path.back();
			auto &scope = imports_[canonical_path(use.module_path)];
			if (scope.find(alias) != scope.end()) {
				diagnostics_.error(use.location, "duplicate import '" + alias + "'");
				continue;
			}

			ImportKind kind;
			if (functions_.find(target_key) != functions_.end()) {
				kind = ImportKind::Function;
				if (!is_function_accessible(functions_.at(target_key), use.module_path,
				                            use.location, target_key)) {
					scope[alias] = ImportInfo{ImportKind::Invalid, target_key,
					                          use.import_path};
					continue;
				}
			} else if (globals_.find(target_key) != globals_.end()) {
				kind = ImportKind::Global;
				if (!is_global_accessible(globals_.at(target_key), use.module_path,
				                          use.location, target_key)) {
					scope[alias] = ImportInfo{ImportKind::Invalid, target_key,
					                          use.import_path};
					continue;
				}
			} else if (modules_.find(target_key) != modules_.end()) {
				kind = ImportKind::Module;
				if (!is_module_accessible(use.import_path, use.module_path,
				                          use.location)) {
					scope[alias] = ImportInfo{ImportKind::Invalid, target_key,
					                          use.import_path};
					continue;
				}
			} else {
				diagnostics_.error(use.location, "unknown import '" + target_key + "'");
				continue;
			}

			scope[alias] = ImportInfo{kind, target_key, use.import_path};
		}
	}

	const std::vector<std::string> &current_module_path() const
	{
		static const std::vector<std::string> empty;
		return current_module_path_ != nullptr ? *current_module_path_ : empty;
	}

	bool is_same_module(const std::vector<std::string> &lhs,
	                    const std::vector<std::string> &rhs) const
	{
		return lhs == rhs;
	}

	bool is_descendant_or_same_module(const std::vector<std::string> &candidate,
	                                  const std::vector<std::string> &ancestor) const
	{
		if (ancestor.size() > candidate.size())
			return false;
		for (std::size_t i = 0; i < ancestor.size(); ++i) {
			if (candidate[i] != ancestor[i])
				return false;
		}
		return true;
	}

	bool is_parent_module_of(const std::vector<std::string> &parent,
	                         const std::vector<std::string> &child) const
	{
		return is_same_module(parent, parent_module_path(child));
	}

	std::vector<std::string> parent_module_path(
	    const std::vector<std::string> &module_path) const
	{
		if (module_path.empty())
			return {};
		return std::vector<std::string>(module_path.begin(), module_path.end() - 1);
	}

	bool can_access_private_module_segment(
	    const std::vector<std::string> &private_module,
	    const std::vector<std::string> &requester) const
	{
		return is_parent_module_of(requester, private_module) ||
		       is_descendant_or_same_module(requester, private_module);
	}

	bool can_access_private_item(const std::vector<std::string> &owner,
	                             const std::vector<std::string> &requester) const
	{
		return is_descendant_or_same_module(requester, owner);
	}

	bool is_module_accessible(const std::vector<std::string> &module_path,
	                          const std::vector<std::string> &requester,
	                          SourceLocation location)
	{
		for (std::size_t size = 1; size <= module_path.size(); ++size) {
			std::vector<std::string> prefix(module_path.begin(),
			                                module_path.begin() + size);
			auto it = modules_.find(canonical_path(prefix));
			if (it == modules_.end() ||
			    it->second.visibility == ast::Visibility::Public)
				continue;

			if (can_access_private_module_segment(prefix, requester))
				continue;

			diagnostics_.error(location,
			                   "private module '" + canonical_path(prefix) + "'");
			return false;
		}
		return true;
	}

	bool is_item_visible(ast::Visibility visibility,
	                     const std::vector<std::string> &owner,
	                     const std::vector<std::string> &requester) const
	{
		return visibility == ast::Visibility::Public ||
		       can_access_private_item(owner, requester);
	}

	bool is_function_accessible(const FunctionInfo &function,
	                            const std::vector<std::string> &requester,
	                            SourceLocation location,
	                            const std::string &display_name)
	{
		if (!is_module_accessible(function.module_path, requester, location))
			return false;
		if (is_item_visible(function.visibility, function.module_path, requester))
			return true;
		diagnostics_.error(location, "private function '" + display_name + "'");
		return false;
	}

	bool is_global_accessible(const GlobalInfo &global,
	                          const std::vector<std::string> &requester,
	                          SourceLocation location,
	                          const std::string &display_name)
	{
		if (!is_module_accessible(global.module_path, requester, location))
			return false;
		if (is_item_visible(global.visibility, global.module_path, requester))
			return true;
		diagnostics_.error(location, "private static '" + display_name + "'");
		return false;
	}

	const ImportInfo *find_import(const std::string &alias) const
	{
		auto scope = imports_.find(canonical_path(current_module_path()));
		if (scope == imports_.end())
			return nullptr;
		auto import = scope->second.find(alias);
		return import != scope->second.end() ? &import->second : nullptr;
	}

	bool has_invalid_import_prefix(const std::vector<std::string> &path) const
	{
		if (path.empty())
			return false;
		auto import = find_import(path[0]);
		return import != nullptr && import->kind == ImportKind::Invalid;
	}

	const FunctionInfo *find_function(const std::vector<std::string> &path) const
	{
		auto it = functions_.find(canonical_path(path));
		return it != functions_.end() ? &it->second : nullptr;
	}

	const FunctionInfo *check_function_candidate(
	    const std::vector<std::string> &display_path,
	    const FunctionInfo *function, SourceLocation location)
	{
		if (function == nullptr)
			return nullptr;
		is_function_accessible(*function, current_module_path(), location,
		                       canonical_path(display_path));
		return function;
	}

	const FunctionInfo *resolve_function(const std::vector<std::string> &path,
	                                     SourceLocation location)
	{
		if (path.empty())
			return nullptr;

		if (path.size() == 1) {
			if (auto import = find_import(path[0]);
			    import != nullptr && import->kind == ImportKind::Function) {
				auto it = functions_.find(import->target_key);
				return it != functions_.end() ? &it->second : nullptr;
			}

			auto local_path = item_path(current_module_path(), path[0]);
			if (auto function = find_function(local_path))
				return check_function_candidate(local_path, function, location);
			return check_function_candidate(path, find_function(path), location);
		}

		if (auto import = find_import(path[0]);
		    import != nullptr && import->kind == ImportKind::Module) {
			auto expanded = import->target_path;
			expanded.insert(expanded.end(), path.begin() + 1, path.end());
			if (auto function = find_function(expanded))
				return check_function_candidate(expanded, function, location);
		}

		auto relative = current_module_path();
		relative.insert(relative.end(), path.begin(), path.end());
		if (auto function = find_function(relative))
			return check_function_candidate(relative, function, location);
		return check_function_candidate(path, find_function(path), location);
	}

	const GlobalInfo *find_global(const std::vector<std::string> &path) const
	{
		auto it = globals_.find(canonical_path(path));
		return it != globals_.end() ? &it->second : nullptr;
	}

	const GlobalInfo *resolve_global(const std::string &name, SourceLocation location)
	{
		if (auto import = find_import(name);
		    import != nullptr && import->kind == ImportKind::Global) {
			auto it = globals_.find(import->target_key);
			return it != globals_.end() ? &it->second : nullptr;
		}

		auto local_path = item_path(current_module_path(), name);
		if (auto global = find_global(local_path)) {
			is_global_accessible(*global, current_module_path(), location,
			                     canonical_path(local_path));
			return global;
		}
		if (auto global = find_global({name})) {
			is_global_accessible(*global, current_module_path(), location, name);
			return global;
		}
		return nullptr;
	}

	void analyze_function(const ast::Function &function)
	{
		current_module_path_ = &function.module_path;
		std::unordered_map<std::string, LocalInfo> locals;

		// FE-102: register this function's generic parameters so check_type
		// recognizes bare identifiers like `T` while walking the body.
		current_generic_parameters_.clear();
		for (const auto &name : function.generic_parameters)
			current_generic_parameters_.insert(name);

		for (const auto &parameter : function.parameters) {
			PrimitiveType parameter_type = check_type(parameter.type);
			if (!locals.emplace(parameter.name, LocalInfo{parameter_type, false}).second)
				diagnostics_.error(parameter.location, "duplicate local '" + parameter.name + "'");
		}

		PrimitiveType return_type = check_type(function.return_type);

		current_function_return_type_ = return_type;
		// `unsafe fn` opens an unsafe context for its entire body; FE-013.
		if (function.is_unsafe)
			++unsafe_depth_;
		for (const auto &statement : function.body)
			analyze_statement(return_type, locals, *statement, 0);
		if (function.is_unsafe)
			--unsafe_depth_;
		current_function_return_type_.reset();
		current_generic_parameters_.clear();
		current_module_path_ = nullptr;
	}

	void analyze_statement(PrimitiveType function_return_type,
	                       std::unordered_map<std::string, LocalInfo> &locals,
	                       const ast::Stmt &statement, int loop_depth)
	{
		if (statement.kind == ast::Stmt::Kind::Let) {
			const auto &let = static_cast<const ast::LetStmt &>(statement);
			PrimitiveType let_type = check_type(let.type);
			bool duplicate = locals.find(let.name) != locals.end();
			if (duplicate)
				diagnostics_.error(let.location, "duplicate local '" + let.name + "'");
			// The new binding is inserted after checking the initializer so
			// `let x: i32 = x;` remains an unknown-name error.
			auto initializer_type = check_expr(locals, *let.initializer, let_type);
			if (initializer_type && !types_compatible(let_type, *initializer_type)) {
				diagnostics_.error(let.location, "initializer type mismatch: expected '" +
				                   format_type(let_type) + "' but got '" +
				                   format_type(*initializer_type) + "'");
			}
			if (!duplicate)
				locals.emplace(let.name, LocalInfo{let_type, let.is_mutable});
			return;
		}

		if (statement.kind == ast::Stmt::Kind::Assign) {
			const auto &assign = static_cast<const ast::AssignStmt &>(statement);
			auto it = locals.find(assign.name);
			if (it == locals.end()) {
				auto global = resolve_global(assign.name, assign.location);
				if (global == nullptr) {
					diagnostics_.error(assign.location, "unknown name '" + assign.name + "'");
					check_expr(locals, *assign.value);
					return;
				}
				if (!global->is_mutable)
					diagnostics_.error(assign.location,
					                   "cannot assign to immutable static '" + assign.name + "'");
				auto value_type = check_expr(locals, *assign.value, global->type);
				if (value_type && !types_compatible(global->type, *value_type)) {
					diagnostics_.error(assign.location, "assignment type mismatch: expected '" +
					                   format_type(global->type) + "' but got '" +
					                   format_type(*value_type) + "'");
				}
				return;
			}
			if (!it->second.is_mutable)
				diagnostics_.error(assign.location,
				                   "cannot assign to immutable local '" + assign.name + "'");
			auto value_type = check_expr(locals, *assign.value, it->second.type);
			if (value_type && !types_compatible(it->second.type, *value_type)) {
				diagnostics_.error(assign.location, "assignment type mismatch: expected '" +
				                   format_type(it->second.type) + "' but got '" +
				                   format_type(*value_type) + "'");
			}
			return;
		}

		if (statement.kind == ast::Stmt::Kind::IndirectAssign) {
			const auto &assign = static_cast<const ast::IndirectAssignStmt &>(statement);
			auto target_type = check_expr(locals, *assign.target);
			if (!target_type) {
				check_expr(locals, *assign.value);
				return;
			}

			auto target_pointee = pointee_type(*target_type);
			if (!target_pointee) {
				diagnostics_.error(assign.location,
				                   "indirect assignment requires pointer target");
				check_expr(locals, *assign.value);
				return;
			}

			// FE-013: `*ptr = value` is a raw deref write — must be unsafe.
			if (options_.enforce_unsafe_blocks && unsafe_depth_ == 0)
				diagnostics_.error(
				    assign.location,
				    "indirect write through raw pointer requires `unsafe` block");

			auto value_type = check_expr(locals, *assign.value, *target_pointee);
			if (value_type && !types_compatible(*target_pointee, *value_type)) {
				diagnostics_.error(assign.location, "assignment type mismatch: expected '" +
				                   format_type(*target_pointee) + "' but got '" +
				                   format_type(*value_type) + "'");
			}
			return;
		}

		if (statement.kind == ast::Stmt::Kind::FieldAssign) {
			const auto &assign = static_cast<const ast::FieldAssignStmt &>(statement);
			// `(*p).field = value` is a write through a raw pointer; FE-013.
			if (options_.enforce_unsafe_blocks && unsafe_depth_ == 0)
				diagnostics_.error(
				    assign.location,
				    "field write through raw pointer requires `unsafe` block");
			// `assign.base` is the pointer expression `p` from `(*p).field = value`.
			auto base_type = check_expr(locals, *assign.base);
			if (!base_type) {
				check_expr(locals, *assign.value);
				return;
			}
			auto pointee = pointee_type(*base_type);
			if (!pointee || !is_user_struct(*pointee)) {
				diagnostics_.error(assign.location,
				                   "field assignment requires pointer-to-struct base");
				check_expr(locals, *assign.value);
				return;
			}
			auto it = structs_.find(pointee->name);
			if (it == structs_.end()) {
				diagnostics_.error(assign.location,
				                   "struct '" + pointee->name + "' not in scope");
				check_expr(locals, *assign.value);
				return;
			}
			auto field_type = it->second.field_type(assign.field);
			if (!field_type) {
				diagnostics_.error(assign.location,
				                   "struct '" + pointee->name + "' has no field '" +
				                       assign.field + "'");
				check_expr(locals, *assign.value);
				return;
			}
			auto value_type = check_expr(locals, *assign.value, *field_type);
			if (value_type && !types_compatible(*field_type, *value_type)) {
				diagnostics_.error(assign.location,
				                   "assignment type mismatch: expected '" +
				                       format_type(*field_type) + "' but got '" +
				                       format_type(*value_type) + "'");
			}
			return;
		}

		if (statement.kind == ast::Stmt::Kind::If) {
			const auto &if_stmt = static_cast<const ast::IfStmt &>(statement);
			auto condition_type = check_expr(locals, *if_stmt.condition, bool_type());
			if (condition_type && *condition_type != bool_type())
				diagnostics_.error(if_stmt.condition->location, "if condition must be bool");

			auto then_locals = locals;
			for (const auto &branch_statement : if_stmt.then_body)
				analyze_statement(function_return_type, then_locals, *branch_statement,
				                  loop_depth);

			auto else_locals = locals;
			for (const auto &branch_statement : if_stmt.else_body)
				analyze_statement(function_return_type, else_locals, *branch_statement,
				                  loop_depth);
			return;
		}

		if (statement.kind == ast::Stmt::Kind::Match) {
			const auto &match_stmt = static_cast<const ast::MatchStmt &>(statement);
			auto value_type = check_expr(locals, *match_stmt.value);
			const EnumInfo *matched_enum = nullptr;
			std::unordered_set<std::string> covered_enum_variants;
			if (value_type && is_user_enum(*value_type)) {
				auto enum_it = enums_.find(value_type->name);
				if (enum_it != enums_.end())
					matched_enum = &enum_it->second;
			}
			bool saw_default = false;
			for (std::size_t i = 0; i < match_stmt.arms.size(); ++i) {
				const auto &arm = match_stmt.arms[i];
				auto arm_locals = locals;
				for (const auto &pattern : arm.patterns) {
					if (saw_default)
						diagnostics_.error(pattern.location,
						                   "match default arm must be last");
					if (pattern.kind == ast::MatchPattern::Kind::Default) {
						if (arm.patterns.size() != 1)
							diagnostics_.error(pattern.location,
							                   "match default pattern must be alone");
						saw_default = true;
					} else if (value_type) {
						if (!pattern.bindings.empty() && arm.patterns.size() != 1)
							diagnostics_.error(pattern.location,
							                   "binding match pattern must be alone");
						auto covered_variant =
						    check_match_pattern(pattern, *value_type, arm_locals);
						if (matched_enum && covered_variant &&
						    covered_variant->enum_info == matched_enum)
							covered_enum_variants.insert(covered_variant->variant->name);
					}
				}

				for (const auto &arm_statement : arm.body)
					analyze_statement(function_return_type, arm_locals, *arm_statement,
					                  loop_depth);
			}
			if (matched_enum && !saw_default) {
				for (const auto &variant : matched_enum->variants) {
					if (covered_enum_variants.find(variant.name) ==
					    covered_enum_variants.end()) {
						diagnostics_.error(
						    variant.location,
						    "non-exhaustive enum match: missing variant '" +
						        variant.name + "'");
					}
				}
			}
			return;
		}

		if (statement.kind == ast::Stmt::Kind::While) {
			const auto &while_stmt = static_cast<const ast::WhileStmt &>(statement);
			auto condition_type = check_expr(locals, *while_stmt.condition, bool_type());
			if (condition_type && *condition_type != bool_type())
				diagnostics_.error(while_stmt.condition->location, "while condition must be bool");

			auto body_locals = locals;
			for (const auto &body_statement : while_stmt.body)
				analyze_statement(function_return_type, body_locals, *body_statement,
				                  loop_depth + 1);
			return;
		}

		if (statement.kind == ast::Stmt::Kind::For) {
			const auto &for_stmt = static_cast<const ast::ForStmt &>(statement);
			auto for_locals = locals;
			analyze_statement(function_return_type, for_locals, *for_stmt.initializer,
			                  loop_depth);

			auto condition_type = check_expr(for_locals, *for_stmt.condition, bool_type());
			if (condition_type && *condition_type != bool_type())
				diagnostics_.error(for_stmt.condition->location, "for condition must be bool");

			auto body_locals = for_locals;
			for (const auto &body_statement : for_stmt.body)
				analyze_statement(function_return_type, body_locals, *body_statement,
				                  loop_depth + 1);

			analyze_statement(function_return_type, for_locals, *for_stmt.increment,
			                  loop_depth);
			return;
		}

		if (statement.kind == ast::Stmt::Kind::Break) {
			if (loop_depth == 0)
				diagnostics_.error(statement.location, "break statement outside loop");
			return;
		}

		if (statement.kind == ast::Stmt::Kind::Continue) {
			if (loop_depth == 0)
				diagnostics_.error(statement.location, "continue statement outside loop");
			return;
		}

		if (statement.kind == ast::Stmt::Kind::UnsafeBlock) {
			const auto &unsafe_block = static_cast<const ast::UnsafeBlockStmt &>(statement);
			++unsafe_depth_;
			for (const auto &inner : unsafe_block.body)
				analyze_statement(function_return_type, locals, *inner, loop_depth);
			--unsafe_depth_;
			return;
		}

		if (statement.kind == ast::Stmt::Kind::Expr) {
			const auto &expr_statement = static_cast<const ast::ExprStmt &>(statement);
			check_expr(locals, *expr_statement.value);
			return;
		}

		const auto &ret = static_cast<const ast::ReturnStmt &>(statement);
		auto value_type = check_expr(locals, *ret.value, function_return_type);
		if (value_type && !types_compatible(function_return_type, *value_type)) {
			diagnostics_.error(ret.location, "return type mismatch: expected '" +
			                   format_type(function_return_type) + "' but got '" +
			                   format_type(*value_type) + "'");
		}
	}

	std::optional<EnumVariantRef> check_match_pattern(
	    const ast::MatchPattern &pattern, PrimitiveType value_type,
	    std::unordered_map<std::string, LocalInfo> &locals)
	{
		switch (pattern.kind) {
		case ast::MatchPattern::Kind::Default:
			return std::nullopt;
		case ast::MatchPattern::Kind::Integer:
			if (!is_integer(value_type)) {
				diagnostics_.error(pattern.location, "match pattern type mismatch");
				return std::nullopt;
			}
			check_integer_literal(pattern.location, value_type, pattern.literal,
			                      pattern.is_negative);
			return std::nullopt;
		case ast::MatchPattern::Kind::Bool:
			if (value_type != bool_type())
				diagnostics_.error(pattern.location, "match pattern type mismatch");
			return std::nullopt;
		case ast::MatchPattern::Kind::Char:
			if (value_type.kind != PrimitiveKind::Char)
				diagnostics_.error(pattern.location, "match pattern type mismatch");
			return std::nullopt;
		case ast::MatchPattern::Kind::Variant:
			return check_enum_match_pattern(pattern, value_type, locals);
		case ast::MatchPattern::Kind::Struct:
			check_struct_match_pattern(pattern, value_type, locals);
			return std::nullopt;
		}
		return std::nullopt;
	}

	std::optional<EnumVariantRef> check_enum_match_pattern(
	    const ast::MatchPattern &pattern, PrimitiveType value_type,
	    std::unordered_map<std::string, LocalInfo> &locals)
	{
		if (!is_user_enum(value_type)) {
			diagnostics_.error(pattern.location, "match pattern type mismatch");
			return std::nullopt;
		}

		bool enum_diagnostic_emitted = false;
		auto ref = resolve_enum_variant(pattern.path, pattern.location, value_type,
		                                enum_diagnostic_emitted);
		if (!ref)
			return std::nullopt;
		if (!types_compatible(enum_type(*ref->enum_info), value_type)) {
			diagnostics_.error(pattern.location, "match pattern type mismatch");
			return std::nullopt;
		}
		if (ref->variant->payload_types.size() != pattern.bindings.size()) {
			diagnostics_.error(
			    pattern.location,
			    "enum variant '" + ref->variant->name + "' pattern expected " +
			        std::to_string(ref->variant->payload_types.size()) +
			        " bindings but got " + std::to_string(pattern.bindings.size()));
			return std::nullopt;
		}
		for (std::size_t i = 0; i < pattern.bindings.size(); ++i)
			bind_match_local(pattern.location, pattern.bindings[i],
			                 ref->variant->payload_types[i], locals);
		return ref;
	}

	void check_struct_match_pattern(const ast::MatchPattern &pattern,
	                                PrimitiveType value_type,
	                                std::unordered_map<std::string, LocalInfo> &locals)
	{
		if (!is_user_struct(value_type) || pattern.path.empty() ||
		    pattern.path[0] != value_type.name) {
			diagnostics_.error(pattern.location, "match pattern type mismatch");
			return;
		}
		auto it = structs_.find(value_type.name);
		if (it == structs_.end())
			return;
		if (it->second.fields.size() != pattern.bindings.size()) {
			diagnostics_.error(
			    pattern.location,
			    "struct '" + value_type.name + "' pattern expected " +
			        std::to_string(it->second.fields.size()) +
			        " bindings but got " + std::to_string(pattern.bindings.size()));
			return;
		}
		for (std::size_t i = 0; i < pattern.bindings.size(); ++i)
			bind_match_local(pattern.location, pattern.bindings[i],
			                 it->second.fields[i].second, locals);
	}

	void bind_match_local(SourceLocation location, const std::string &name,
	                      PrimitiveType type,
	                      std::unordered_map<std::string, LocalInfo> &locals)
	{
		if (name == "_")
			return;
		if (!locals.emplace(name, LocalInfo{type, false}).second)
			diagnostics_.error(location, "duplicate local '" + name + "'");
	}

	std::optional<PrimitiveType> check_expr(
		const std::unordered_map<std::string, LocalInfo> &locals, const ast::Expr &expr,
		std::optional<PrimitiveType> expected = std::nullopt)
	{
		// Expected types let unadorned integer literals inherit context from
		// initializers, returns, calls, and unary expressions.
		switch (expr.kind) {
		case ast::Expr::Kind::Integer: {
			const auto &integer = static_cast<const ast::IntegerExpr &>(expr);
			if (expected && is_integer(*expected)) {
				check_integer_literal(expr.location, *expected, integer.literal, false);
				return expected;
			}
			check_integer_literal(expr.location, i32_type(), integer.literal, false);
			return i32_type();
		}
		case ast::Expr::Kind::Bool:
			return PrimitiveType{PrimitiveKind::Bool};
		case ast::Expr::Kind::Char:
			return PrimitiveType{PrimitiveKind::Char};
		case ast::Expr::Kind::String:
			return PrimitiveType{PrimitiveKind::Str};
		case ast::Expr::Kind::Name: {
			const auto &name = static_cast<const ast::NameExpr &>(expr);
			auto it = locals.find(name.name);
			if (it != locals.end())
				return it->second.type;
			auto global = resolve_global(name.name, name.location);
			if (global != nullptr)
				return global->type;
			diagnostics_.error(name.location, "unknown name '" + name.name + "'");
			return std::nullopt;
		}
		case ast::Expr::Kind::Binary: {
			const auto &binary = static_cast<const ast::BinaryExpr &>(expr);
			if (is_logical_operator(binary.op))
				return check_logical_binary_expr(locals, binary);

			auto lhs_type = check_expr(locals, *binary.lhs, expected);
			auto rhs_type = check_expr(locals, *binary.rhs, lhs_type ? lhs_type : expected);
			if (!lhs_type || !rhs_type)
				return lhs_type ? lhs_type : rhs_type;
			if (is_comparison_operator(binary.op)) {
				if (!is_integer(*lhs_type) || !is_integer(*rhs_type)) {
					diagnostics_.error(binary.location, "comparison requires integer operands");
					return bool_type();
				}
				if (*lhs_type != *rhs_type)
					diagnostics_.error(binary.location,
					                   "comparison operands must have the same type");
				return bool_type();
			}
			if (is_pointer_arithmetic(binary.op, *lhs_type)) {
				if (!is_integer(*rhs_type)) {
					diagnostics_.error(binary.location,
					                   "pointer arithmetic requires integer offset");
				}
				return pointer_arithmetic_type(*lhs_type);
			}
			if (!is_integer(*lhs_type) || !is_integer(*rhs_type)) {
				diagnostics_.error(binary.location, "arithmetic requires integer operands");
				return *lhs_type;
			}
			if (*lhs_type != *rhs_type) {
				diagnostics_.error(binary.location, "arithmetic operands must have the same type");
				return *lhs_type;
			}
			return lhs_type;
		}
		case ast::Expr::Kind::Cast: {
			const auto &cast = static_cast<const ast::CastExpr &>(expr);
			return check_cast_expr(locals, cast);
		}
		case ast::Expr::Kind::Unary: {
			const auto &unary = static_cast<const ast::UnaryExpr &>(expr);
			return check_unary_expr(locals, unary, expected);
		}
		case ast::Expr::Kind::Call: {
			const auto &call = static_cast<const ast::CallExpr &>(expr);
			if (call.callee_path.size() == 1 && call.callee_path[0] == "len")
				return check_len_call(locals, call);
			auto function = resolve_function(call.callee_path, call.location);
			if (function == nullptr) {
				if (auto builtin_variant = check_builtin_option_result_call(
				        locals, call, expected)) {
					return builtin_variant;
				}
				bool enum_diagnostic_emitted = false;
				if (auto variant = resolve_enum_variant(
				        call.callee_path, call.location, expected,
				        enum_diagnostic_emitted)) {
					return check_enum_variant_call(locals, call, *variant);
				}
				if (!enum_diagnostic_emitted && !has_invalid_import_prefix(call.callee_path))
					diagnostics_.error(call.location,
					                   "unknown function '" + call.callee + "'");
			} else {
				// FE-013: extern fn calls cross an unchecked ABI boundary and
				// must be inside an `unsafe` block (or `unsafe fn`).
				if (function->is_extern && options_.enforce_unsafe_blocks &&
				    unsafe_depth_ == 0) {
					diagnostics_.error(
					    call.location,
					    "call to extern function '" + call.callee +
					        "' requires `unsafe` block");
				}
				std::size_t expected_count = function->parameter_types.size();
				if (expected_count != call.arguments.size()) {
					diagnostics_.error(call.location, "function '" + call.callee + "' expected " +
					                   std::to_string(expected_count) + " arguments but got " +
					                   std::to_string(call.arguments.size()));
				}

				// FE-103: generic functions need type-arg inference; we skip
				// strict types_compatible matching here and instead unify
				// the parameter pattern against each actual arg's type.
				if (!function->generic_parameters.empty()) {
					std::unordered_set<std::string> generic_names(
					    function->generic_parameters.begin(),
					    function->generic_parameters.end());
					std::unordered_map<std::string, PrimitiveType> bindings;
					bool inference_ok = true;
					for (std::size_t i = 0; i < call.arguments.size(); ++i) {
						auto argument_type =
						    check_expr(locals, *call.arguments[i]);
						if (!argument_type || i >= expected_count) {
							inference_ok = false;
							continue;
						}
						if (!unify_generic_pattern(
						        function->parameter_types[i], *argument_type,
						        generic_names, bindings)) {
							diagnostics_.error(
							    call.arguments[i]->location,
							    "cannot infer generic type for parameter " +
							        std::to_string(i) + ": pattern '" +
							        format_type(function->parameter_types[i]) +
							        "' does not match argument type '" +
							        format_type(*argument_type) + "'");
							inference_ok = false;
						}
					}
					for (const auto &name : function->generic_parameters) {
						if (bindings.find(name) == bindings.end()) {
							diagnostics_.error(
							    call.location,
							    "cannot infer generic type parameter '" + name +
							        "' for call to '" + call.callee + "'");
							inference_ok = false;
						}
					}
					if (!inference_ok)
						return std::nullopt;
					// FE-103.1: re-route through resolve_substituted_type so
					// a return type like `Box<T>` mangles to `Box__i32` and
					// matches the consumer's `Box<i32>` annotation.
					return resolve_substituted_type(
					    substitute_generics(function->return_type, bindings),
					    call.location);
				}

				for (std::size_t i = 0; i < call.arguments.size(); ++i) {
					std::optional<PrimitiveType> parameter_type;
					if (i < expected_count)
						parameter_type = function->parameter_types[i];
					auto argument_type = check_expr(locals, *call.arguments[i], parameter_type);
					if (parameter_type && argument_type &&
					    !types_compatible(*parameter_type, *argument_type)) {
						diagnostics_.error(call.arguments[i]->location,
						                   "argument type mismatch: expected '" +
						                   format_type(*parameter_type) + "' but got '" +
						                   format_type(*argument_type) + "'");
					}
				}
				return function->return_type;
			}
			for (const auto &argument : call.arguments)
				check_expr(locals, *argument);
			return i32_type();
		}
		case ast::Expr::Kind::StructLiteral: {
			const auto &literal = static_cast<const ast::StructLiteralExpr &>(expr);
			PrimitiveType literal_type = check_type(literal.type);
			// FE-103: a literal `Box { value: 7 }` written without explicit
			// type args is generic. If the expected type is a monomorph of
			// the same template (e.g. Box__i32), adopt that here so field
			// types are concrete during the per-field checks below.
			if (is_user_struct(literal_type) && expected && is_user_struct(*expected)) {
				if (auto info = structs_.find(literal_type.name);
				    info != structs_.end() && info->second.is_generic) {
					if (auto target = structs_.find(expected->name);
					    target != structs_.end() &&
					    target->second.template_name == literal_type.name) {
						literal_type = *expected;
					}
					// FE-103.1: expected may be the unmangled angle-bracket
					// form `Box<T>` produced by check_type_name when the
					// surrounding scope still has T unbound. The literal's
					// concrete type must agree with that expected form, so
					// adopt it directly as the literal type.
					else if (auto open = expected->name.find('<');
					         open != std::string::npos &&
					         !expected->name.empty() &&
					         expected->name.back() == '>' &&
					         expected->name.substr(0, open) == literal_type.name) {
						literal_type = *expected;
					}
				}
			}
			if (!is_user_struct(literal_type)) {
				for (const auto &field : literal.fields)
					check_expr(locals, *field.value);
				return literal_type;
			}

			// FE-103.1: when the literal type is an unmangled `Box<T>` form
			// (T is an unbound generic param in scope), look up the
			// underlying template for field iteration. Field types remain
			// in their raw template form (referencing T) and compare via
			// the generic-type-variable plumbing.
			std::string lookup_name = literal_type.name;
			if (auto open = lookup_name.find('<');
			    open != std::string::npos && !lookup_name.empty() &&
			    lookup_name.back() == '>') {
				std::string base = lookup_name.substr(0, open);
				if (auto tpl = structs_.find(base);
				    tpl != structs_.end() && tpl->second.is_generic)
					lookup_name = base;
			}
			auto it = structs_.find(lookup_name);
			if (it == structs_.end()) {
				diagnostics_.error(literal.location,
				                   "struct '" + literal_type.name + "' not in scope");
				for (const auto &field : literal.fields)
					check_expr(locals, *field.value);
				return literal_type;
			}

			std::unordered_set<std::string> seen_fields;
			for (const auto &field : literal.fields) {
				if (!seen_fields.insert(field.name).second) {
					diagnostics_.error(field.location,
					                   "duplicate field '" + field.name +
					                       "' in struct literal");
					check_expr(locals, *field.value);
					continue;
				}
				auto field_type = it->second.field_type(field.name);
				if (!field_type) {
					diagnostics_.error(field.location,
					                   "struct '" + literal_type.name + "' has no field '" +
					                       field.name + "'");
					check_expr(locals, *field.value);
					continue;
				}
				auto value_type = check_expr(locals, *field.value, *field_type);
				if (value_type && !types_compatible(*field_type, *value_type)) {
					diagnostics_.error(field.location,
					                   "field type mismatch: expected '" +
					                       format_type(*field_type) + "' but got '" +
					                       format_type(*value_type) + "'");
				}
			}

			for (const auto &declared_field : it->second.fields) {
				if (seen_fields.find(declared_field.first) == seen_fields.end()) {
					diagnostics_.error(literal.location,
					                   "missing field '" + declared_field.first +
					                       "' in struct literal");
				}
			}
			if (expected && !types_compatible(*expected, literal_type)) {
				diagnostics_.error(literal.location,
				                   "initializer type mismatch: expected '" +
				                       format_type(*expected) + "' but got '" +
				                       format_type(literal_type) + "'");
			}
			return literal_type;
		}
		case ast::Expr::Kind::Tuple: {
			const auto &tuple = static_cast<const ast::TupleExpr &>(expr);
			std::optional<std::vector<PrimitiveType>> expected_elements;
			if (expected && is_tuple(*expected))
				expected_elements = tuple_elements(*expected);

			if (expected_elements && expected_elements->size() != tuple.elements.size()) {
				diagnostics_.error(
				    tuple.location,
				    "tuple initializer expected " +
				        std::to_string(expected_elements->size()) +
				        " elements but got " + std::to_string(tuple.elements.size()));
			}

			std::vector<PrimitiveType> element_types;
			element_types.reserve(tuple.elements.size());
			for (std::size_t i = 0; i < tuple.elements.size(); ++i) {
				std::optional<PrimitiveType> element_expected;
				if (expected_elements && i < expected_elements->size())
					element_expected = (*expected_elements)[i];
				auto value_type = check_expr(locals, *tuple.elements[i], element_expected);
				if (!value_type) {
					element_types.push_back(i32_type());
					continue;
				}
				if (element_expected && !types_compatible(*element_expected, *value_type)) {
					diagnostics_.error(
					    tuple.elements[i]->location,
					    "tuple element type mismatch: expected '" +
					        format_type(*element_expected) + "' but got '" +
					        format_type(*value_type) + "'");
				}
				element_types.push_back(*value_type);
			}
			return tuple_type(std::move(element_types));
		}
		case ast::Expr::Kind::FieldAccess: {
			const auto &access = static_cast<const ast::FieldAccessExpr &>(expr);
			auto base_type = check_expr(locals, *access.base);
			if (!base_type)
				return std::nullopt;
			if (is_tuple(*base_type)) {
				auto elements = tuple_elements(*base_type);
				auto index = parse_tuple_field_index(access.field);
				if (!elements || !index) {
					diagnostics_.error(access.location,
					                   "tuple field access requires numeric field");
					return std::nullopt;
				}
				if (*index >= elements->size()) {
					diagnostics_.error(access.location,
					                   "tuple has no field '" + access.field + "'");
					return std::nullopt;
				}
				return (*elements)[*index];
			}
			if (!is_user_struct(*base_type)) {
				diagnostics_.error(access.location,
				                   "field access requires struct value, got '" +
				                       format_type(*base_type) + "'");
				return std::nullopt;
			}
			auto it = structs_.find(base_type->name);
			if (it == structs_.end()) {
				diagnostics_.error(access.location,
				                   "struct '" + base_type->name + "' not in scope");
				return std::nullopt;
			}
			auto field_type = it->second.field_type(access.field);
			if (!field_type) {
				diagnostics_.error(access.location,
				                   "struct '" + base_type->name + "' has no field '" +
				                       access.field + "'");
				return std::nullopt;
			}
			return field_type;
		}
		case ast::Expr::Kind::Index: {
			const auto &index = static_cast<const ast::IndexExpr &>(expr);
			return check_index_expr(locals, index);
		}
		case ast::Expr::Kind::Try: {
			const auto &try_expr = static_cast<const ast::TryExpr &>(expr);
			return check_try_expr(locals, try_expr);
		}
		default:
			break;
		}

		return i32_type();
	}

	std::optional<PrimitiveType> check_try_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::TryExpr &try_expr)
	{
		auto operand_type = check_expr(locals, *try_expr.operand);
		if (!operand_type)
			return std::nullopt;

		if (!is_result(*operand_type)) {
			diagnostics_.error(try_expr.location,
			                   "'?' operator requires Result operand, got '" +
			                       format_type(*operand_type) + "'");
			return std::nullopt;
		}

		if (!current_function_return_type_) {
			diagnostics_.error(try_expr.location,
			                   "'?' operator used outside of a function");
			return std::nullopt;
		}

		if (!is_result(*current_function_return_type_)) {
			diagnostics_.error(
			    try_expr.location,
			    "'?' operator requires enclosing function to return Result, got '" +
			        format_type(*current_function_return_type_) + "'");
			return std::nullopt;
		}

		auto operand_error = result_error_type(*operand_type).value_or(i32_type());
		auto return_error = result_error_type(*current_function_return_type_)
		                        .value_or(i32_type());
		if (!types_compatible(return_error, operand_error)) {
			diagnostics_.error(
			    try_expr.location,
			    "'?' error type mismatch: function returns '" +
			        format_type(*current_function_return_type_) +
			        "' but operand is '" + format_type(*operand_type) + "'");
			return std::nullopt;
		}

		auto payload = handle_payload_type(*operand_type);
		if (!payload)
			return i32_type();
		return payload;
	}

	std::optional<PrimitiveType> check_len_call(
		const std::unordered_map<std::string, LocalInfo> &locals, const ast::CallExpr &call)
	{
		if (call.arguments.size() != 1) {
			diagnostics_.error(call.location, "len() expected 1 argument but got " +
			                   std::to_string(call.arguments.size()));
			for (const auto &argument : call.arguments)
				check_expr(locals, *argument);
			return i32_type();
		}
		auto argument_type = check_expr(locals, *call.arguments[0]);
		if (!argument_type)
			return i32_type();
		if (argument_type->kind == PrimitiveKind::Str)
			return i32_type();
		if (!is_slice(*argument_type)) {
			diagnostics_.error(call.location, "len() requires slice or str argument");
			return i32_type();
		}
		auto element = handle_payload_type(*argument_type);
		if (!element || !slice_helper_type_suffix(*element)) {
			diagnostics_.error(call.location,
			                   "len() does not support slice element type '" +
			                       (element ? format_type(*element) : std::string("")) + "'");
		}
		return i32_type();
	}

	std::optional<PrimitiveType> check_index_expr(
		const std::unordered_map<std::string, LocalInfo> &locals, const ast::IndexExpr &index)
	{
		auto base_type = check_expr(locals, *index.base);
		auto index_type = check_expr(locals, *index.index, i32_type());
		if (index_type && !is_integer(*index_type))
			diagnostics_.error(index.index->location, "index expression requires integer index");
		if (!base_type)
			return std::nullopt;
		if (is_negative_integer_literal(*index.index) && is_slice(*base_type))
			diagnostics_.error(index.index->location, "slice index out of bounds");
		if (is_slice(*base_type)) {
			auto element = handle_payload_type(*base_type);
			if (!element)
				return std::nullopt;
			if (!slice_helper_type_suffix(*element)) {
				diagnostics_.error(index.location,
				                   "slice indexing does not support element type '" +
				                       format_type(*element) + "'");
			}
			return element;
		}
		if (base_type->kind == PrimitiveKind::Str)
			return u8_type();
		auto pointee = pointee_type(*base_type);
		if (!pointee) {
			diagnostics_.error(index.location, "index expression requires pointer, str, or slice base");
			return std::nullopt;
		}
		return pointee;
	}

	std::optional<PrimitiveType> check_unary_expr(
		const std::unordered_map<std::string, LocalInfo> &locals, const ast::UnaryExpr &unary,
		std::optional<PrimitiveType> expected)
	{
		if (unary.op == "!")
			return check_logical_not_expr(locals, unary);
		if (unary.op == "pre++" || unary.op == "post++" ||
		    unary.op == "pre--" || unary.op == "post--")
			return check_increment_expr(locals, unary);
		if (unary.op == "&")
			return check_address_of_expr(locals, unary);
		if (unary.op == "*")
			return check_deref_expr(locals, unary);

		if (unary.op != "-")
			return check_expr(locals, *unary.operand, expected);

		std::optional<PrimitiveType> operand_expected = expected;
		if (expected && !is_signed_integer(*expected)) {
			diagnostics_.error(unary.location, "unary '-' requires a signed integer operand");
			operand_expected = std::nullopt;
		}

		if (unary.operand->kind == ast::Expr::Kind::Integer && operand_expected &&
		    is_signed_integer(*operand_expected)) {
			const auto &integer = static_cast<const ast::IntegerExpr &>(*unary.operand);
			check_integer_literal(unary.location, *operand_expected, integer.literal, true);
			return operand_expected;
		}

		auto operand_type = check_expr(locals, *unary.operand, operand_expected);
		if (!operand_type)
			return std::nullopt;
		if (!is_signed_integer(*operand_type)) {
			diagnostics_.error(unary.location, "unary '-' requires a signed integer operand");
			return operand_type;
		}
		return operand_type;
	}

	std::optional<PrimitiveType> check_increment_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::UnaryExpr &unary)
	{
		if (unary.operand->kind != ast::Expr::Kind::Name) {
			diagnostics_.error(unary.location, "increment requires mutable integer name");
			check_expr(locals, *unary.operand);
			return std::nullopt;
		}

		const auto &name = static_cast<const ast::NameExpr &>(*unary.operand);
		auto it = locals.find(name.name);
		if (it != locals.end()) {
			if (!it->second.is_mutable) {
				diagnostics_.error(unary.location,
				                   "cannot increment immutable local '" + name.name + "'");
			}
			if (!is_integer(it->second.type)) {
				diagnostics_.error(unary.location,
				                   "increment requires integer operand");
			}
			return it->second.type;
		}

		auto global = resolve_global(name.name, name.location);
		if (global == nullptr) {
			diagnostics_.error(name.location, "unknown name '" + name.name + "'");
			return std::nullopt;
		}
		if (!global->is_mutable) {
			diagnostics_.error(unary.location,
			                   "cannot increment immutable static '" + name.name + "'");
		}
		if (!is_integer(global->type)) {
			diagnostics_.error(unary.location, "increment requires integer operand");
		}
		return global->type;
	}

	std::optional<PrimitiveType> check_logical_binary_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::BinaryExpr &binary)
	{
		auto lhs_type = check_expr(locals, *binary.lhs, bool_type());
		auto rhs_type = check_expr(locals, *binary.rhs, bool_type());
		if (!lhs_type || !rhs_type)
			return bool_type();
		if (*lhs_type != bool_type() || *rhs_type != bool_type())
			diagnostics_.error(binary.location, "logical operator requires bool operands");
		return bool_type();
	}

	bool is_pointer_arithmetic(const std::string &op, PrimitiveType lhs_type) const
	{
		return (is_pointer(lhs_type) || lhs_type.kind == PrimitiveKind::Str) &&
		       (op == "+" || op == "-");
	}

	PrimitiveType pointer_arithmetic_type(PrimitiveType lhs_type) const
	{
		if (lhs_type.kind == PrimitiveKind::Str)
			return pointer_to(u8_type());
		return lhs_type;
	}

	std::optional<PrimitiveType> check_address_of_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::UnaryExpr &unary)
	{
		if (unary.operand->kind != ast::Expr::Kind::Name) {
			diagnostics_.error(unary.location, "address-of requires local name");
			check_expr(locals, *unary.operand);
			return std::nullopt;
		}

		const auto &name = static_cast<const ast::NameExpr &>(*unary.operand);
		auto it = locals.find(name.name);
		if (it == locals.end()) {
			diagnostics_.error(name.location, "unknown name '" + name.name + "'");
			return std::nullopt;
		}
		if (!it->second.is_mutable)
			diagnostics_.error(unary.location,
			                   "address-of requires mutable local '" + name.name + "'");
		return pointer_to(it->second.type);
	}

	std::optional<PrimitiveType> check_deref_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::UnaryExpr &unary)
	{
		auto operand_type = check_expr(locals, *unary.operand);
		if (!operand_type)
			return std::nullopt;
		auto target_type = pointee_type(*operand_type);
		if (!target_type) {
			diagnostics_.error(unary.location, "dereference requires pointer operand");
			return operand_type;
		}
		// FE-013: raw pointer deref must be inside an `unsafe` block or
		// `unsafe fn`. Slice/string/owned-str access goes through their own
		// helpers and is safe — only the bare `*ptr` form lands here.
		if (options_.enforce_unsafe_blocks && unsafe_depth_ == 0)
			diagnostics_.error(
			    unary.location,
			    "dereference of raw pointer requires `unsafe` block");
		return target_type;
	}

	std::optional<PrimitiveType> check_cast_expr(
		const std::unordered_map<std::string, LocalInfo> &locals, const ast::CastExpr &cast)
	{
		auto source_type = check_expr(locals, *cast.value);
		PrimitiveType target_type = check_type(cast.target);
		if (!source_type)
			return target_type;
		if (!is_cast_allowed(*source_type, target_type)) {
			diagnostics_.error(cast.location, "cannot cast '" + format_type(*source_type) +
			                   "' to '" + format_type(target_type) + "'");
		}
		return target_type;
	}

	bool is_cast_allowed(PrimitiveType source, PrimitiveType target) const
	{
		if (source == target && source.kind != PrimitiveKind::Str)
			return true;
		if (source.kind == PrimitiveKind::Str && target.kind == PrimitiveKind::OwnedStr)
			return true;
		if (source.kind == PrimitiveKind::OwnedStr && target.kind == PrimitiveKind::Str)
			return true;
		if ((is_pointer(source) && is_handle(target)) || (is_handle(source) && is_pointer(target)))
			return true;
		if (is_vector(source) && is_slice(target)) {
			auto source_payload = handle_payload_type(source);
			auto target_payload = handle_payload_type(target);
			return source_payload && target_payload && *source_payload == *target_payload;
		}
		if (source == pointer_to(u8_type()) && target.kind == PrimitiveKind::Str)
			return true;
		if (is_pointer(source) && is_pointer(target))
			return true;
		if (is_integer(source) && is_integer(target))
			return true;
		if (source.kind == PrimitiveKind::Bool && is_integer(target))
			return true;
		if (is_integer(source) && (is_pointer(target) || target.kind == PrimitiveKind::Str))
			return true;
		if ((is_pointer(source) || source.kind == PrimitiveKind::Str) && is_integer(target))
			return true;
		PrimitiveType char_scalar_type{PrimitiveKind::UnsignedInteger, 32};
		if (source.kind == PrimitiveKind::Char && target == char_scalar_type)
			return true;
		return false;
	}

	bool types_compatible(PrimitiveType expected, PrimitiveType actual) const
	{
		if (expected == actual)
			return true;
		return expected.kind == PrimitiveKind::Str && actual.kind == PrimitiveKind::OwnedStr;
	}

	std::optional<PrimitiveType> check_logical_not_expr(
		const std::unordered_map<std::string, LocalInfo> &locals,
		const ast::UnaryExpr &unary)
	{
		auto operand_type = check_expr(locals, *unary.operand, bool_type());
		if (!operand_type)
			return std::nullopt;
		if (*operand_type != bool_type())
			diagnostics_.error(unary.location, "unary '!' requires a bool operand");
		return bool_type();
	}

	void check_integer_literal(SourceLocation location, PrimitiveType type,
	                           const std::string &literal, bool is_negative)
	{
		bool fits = false;
		auto magnitude = parse_decimal_magnitude(literal);
		if (magnitude) {
			if (is_unsigned_integer(type)) {
				fits = !is_negative && unsigned_integer_literal_fits(type, *magnitude);
			} else if (is_signed_integer(type)) {
				std::uint64_t max = max_signed_magnitude(type);
				fits = is_negative ? *magnitude <= max + 1 : *magnitude <= max;
			}
		}

		if (!fits)
			diagnostics_.error(location, "integer literal does not fit type '" + format_type(type) + "'");
	}

	PrimitiveType check_type(const ast::TypeName &type)
	{
		return check_type_name(type.name, type.location);
	}

	// FE-103.1: re-route a post-`substitute_generics` UserStruct that may
	// carry an unmangled angle-bracket name like "Box<i32>" through
	// check_type_name so the nested generic instantiation gets mangled
	// (`Box__i32`), registered, and given proper layout bits. Non-UserStruct
	// types and UserStructs without angle brackets pass through unchanged.
	PrimitiveType resolve_substituted_type(PrimitiveType type,
	                                       const SourceLocation &loc)
	{
		if (type.kind != PrimitiveKind::UserStruct)
			return type;
		if (type.name.find('<') == std::string::npos)
			return type;
		return check_type_name(type.name, loc);
	}

	// FE-103: monomorphize a generic struct instantiation like `Box<i32>`.
	// Mangles a unique name, substitutes field types, computes layout,
	// and registers the result as a non-generic StructInfo so subsequent
	// uses (literals, field access) work via the existing code paths.
	// Recursive self-references (struct Tree<T> { left: *Tree<T> }) are
	// not yet supported and would require pre-registering the mangled
	// name before substituting fields.
	PrimitiveType instantiate_generic_struct(
	    const std::string &base,
	    const std::vector<std::string> &type_arg_names,
	    const SourceLocation &loc)
	{
		auto template_it = structs_.find(base);
		if (template_it == structs_.end() || !template_it->second.is_generic) {
			diagnostics_.error(loc,
			                   "'" + base + "' is not a generic struct");
			return i32_type();
		}
		const auto &tpl = template_it->second;
		if (tpl.generic_parameters.size() != type_arg_names.size()) {
			diagnostics_.error(loc, "generic struct '" + base + "' expects " +
			                            std::to_string(tpl.generic_parameters.size()) +
			                            " type arguments but got " +
			                            std::to_string(type_arg_names.size()));
			return i32_type();
		}
		std::unordered_map<std::string, PrimitiveType> bindings;
		for (std::size_t i = 0; i < tpl.generic_parameters.size(); ++i)
			bindings[tpl.generic_parameters[i]] =
			    check_type_name(type_arg_names[i], loc);
		std::string mangled =
		    base + mangle_generic_suffix(tpl.generic_parameters, bindings);
		if (auto existing = structs_.find(mangled); existing != structs_.end())
			return struct_type(mangled, existing->second);
		StructInfo info;
		info.location = tpl.location;
		info.module_path = tpl.module_path;
		info.visibility = tpl.visibility;
		info.template_name = base;
		info.is_generic = false;
		for (const auto &field : tpl.fields) {
			PrimitiveType field_type =
			    substitute_generics(field.second, bindings);
			// FE-103.1: substitute_generics may produce an angle-bracket
			// form like `Box<i32>` that needs re-routing through
			// check_type_name to mangle to `Box__i32` and register the
			// nested monomorph (and pick up its layout bits).
			info.fields.emplace_back(field.first,
			                         resolve_substituted_type(field_type, loc));
		}
		compute_struct_layout(info);
		structs_[mangled] = std::move(info);
		return struct_type(mangled, structs_.at(mangled));
	}

	PrimitiveType check_type_name(const std::string &name, const SourceLocation &loc)
	{
		if (auto option_args = consume_generic_type_arguments(name, "Option")) {
			if (option_args->size() == 1)
				return option_of(check_type_name((*option_args)[0], loc));
			diagnostics_.error(loc, "Option expects 1 type argument");
			return option_of(i32_type());
		}
		if (auto result_args = consume_generic_type_arguments(name, "Result")) {
			if (result_args->size() == 1)
				return result_of(check_type_name((*result_args)[0], loc));
			if (result_args->size() == 2) {
				return result_of(check_type_name((*result_args)[0], loc),
				                 check_type_name((*result_args)[1], loc));
			}
			diagnostics_.error(loc, "Result expects 1 or 2 type arguments");
			return result_of(i32_type());
		}
		auto primitive_type = parse_primitive_type(name);
		if (primitive_type)
			return *primitive_type;
		if (!name.empty() && name.front() == '*')
			return pointer_to(check_type_name(name.substr(1), loc));
		if (auto tuple_names = split_tuple_type_name(name)) {
			std::vector<PrimitiveType> elements;
			elements.reserve(tuple_names->size());
			for (const auto &element_name : *tuple_names)
				elements.push_back(check_type_name(element_name, loc));
			return tuple_type(std::move(elements));
		}
		if (structs_.find(name) != structs_.end())
			return struct_type(name, structs_.at(name));
		if (auto it = enums_.find(name); it != enums_.end())
			return enum_type(it->second);
		// FE-102: a bare identifier matching a generic parameter in scope
		// resolves to a type variable. We model it as a UserStruct with the
		// parameter name; layout/codegen for generic bodies is gated on
		// FE-103 monomorphization, so callers must avoid size queries here.
		if (current_generic_parameters_.count(name) > 0)
			return user_struct_type(name);
		// FE-103: handle a generic struct instantiation like `Box<i32>`
		// by mangling and registering a monomorphized StructInfo.
		if (auto open = name.find('<');
		    open != std::string::npos && !name.empty() && name.back() == '>') {
			std::string base = name.substr(0, open);
			auto template_it = structs_.find(base);
			if (template_it != structs_.end() && template_it->second.is_generic) {
				if (auto args = consume_generic_type_arguments(name, base)) {
					// FE-103.1: defer mangling when an arg is itself an
					// unbound generic type variable in the current scope
					// (e.g. `Box<T>` inside `fn wrap<T>(...)`). Eagerly
					// mangling produces `Box__T`, which substitute_generics
					// can't re-walk to `Box__i32` later. Keeping the angle-
					// bracket name lets substitute_generics handle the
					// nested template form via substitute_generic_name.
					bool has_unbound_param = false;
					for (const auto &arg : *args) {
						if (current_generic_parameters_.count(arg) > 0) {
							has_unbound_param = true;
							break;
						}
					}
					if (has_unbound_param)
						return user_struct_type(name);
					return instantiate_generic_struct(base, *args, loc);
				}
			}
		}
		if (name == "int") {
			diagnostics_.error(
			    loc, "unknown type '" + name + "'",
			    {FixIt{"replace with 'i32'", SourceSpan::from_location(loc, name.size()),
			           "i32"}});
			return i32_type();
		}
		diagnostics_.error(loc, "unknown type '" + name + "'");
		return i32_type();
	}

	static std::size_t align_up(std::size_t value, std::size_t alignment)
	{
		if (alignment <= 1)
			return value;
		return (value + alignment - 1) / alignment * alignment;
	}

	void compute_struct_layout(StructInfo &info)
	{
		std::size_t offset = 0;
		std::size_t struct_alignment = 1;
		for (const auto &field : info.fields) {
			std::size_t field_alignment = type_alignment_bytes(field.second).value_or(1u);
			std::size_t field_size = type_size_bytes(field.second).value_or(0u);
			offset = align_up(offset, field_alignment);
			offset += field_size;
			if (field_alignment > struct_alignment)
				struct_alignment = field_alignment;
		}
		info.alignment = struct_alignment;
		info.total_size = align_up(offset, struct_alignment);
	}

	PrimitiveType struct_type(const std::string &name, const StructInfo &info) const
	{
		return user_struct_type(name, static_cast<int>(info.total_size * 8));
	}

	PrimitiveType enum_type(const EnumInfo &info) const
	{
		return user_enum_type(info.name,
		                      static_cast<int>(info.layout.total_size * 8));
	}

	std::optional<std::string> builtin_constructor_name(
	    const std::vector<std::string> &path, PrimitiveType expected) const
	{
		if (path.size() == 1)
			return path[0];
		if (path.size() != 2)
			return std::nullopt;
		if (is_option(expected) && path[0] == "Option")
			return path[1];
		if (is_result(expected) && path[0] == "Result")
			return path[1];
		return std::nullopt;
	}

	std::optional<PrimitiveType> check_builtin_option_result_call(
	    const std::unordered_map<std::string, LocalInfo> &locals,
	    const ast::CallExpr &call, std::optional<PrimitiveType> expected)
	{
		if (!expected || (!is_option(*expected) && !is_result(*expected)))
			return std::nullopt;
		auto constructor = builtin_constructor_name(call.callee_path, *expected);
		if (!constructor)
			return std::nullopt;

		std::optional<PrimitiveType> payload_type;
		if (is_option(*expected)) {
			if (*constructor == "None") {
				payload_type = std::nullopt;
			} else if (*constructor == "Some") {
				payload_type = handle_payload_type(*expected);
			} else {
				return std::nullopt;
			}
		} else if (*constructor == "Ok") {
			payload_type = handle_payload_type(*expected);
		} else if (*constructor == "Err") {
			payload_type = result_error_type(*expected).value_or(i32_type());
		} else {
			return std::nullopt;
		}

		std::size_t expected_count = payload_type ? 1u : 0u;
		if (expected_count != call.arguments.size()) {
			diagnostics_.error(
			    call.location,
			    "enum variant '" + *constructor + "' expected " +
			        std::to_string(expected_count) + " arguments but got " +
			        std::to_string(call.arguments.size()));
		}
		for (std::size_t i = 0; i < call.arguments.size(); ++i) {
			auto argument_type = check_expr(locals, *call.arguments[i], payload_type);
			if (payload_type && argument_type &&
			    !types_compatible(*payload_type, *argument_type)) {
				diagnostics_.error(
				    call.arguments[i]->location,
				    "enum payload type mismatch: expected '" +
				        format_type(*payload_type) + "' but got '" +
				        format_type(*argument_type) + "'");
			}
		}
		return expected;
	}

	std::optional<EnumVariantRef> resolve_enum_variant(
	    const std::vector<std::string> &path, SourceLocation location,
	    std::optional<PrimitiveType> expected, bool &diagnostic_emitted)
	{
		if (path.empty())
			return std::nullopt;

		if (path.size() == 2) {
			auto enum_it = enums_.find(path[0]);
			if (enum_it == enums_.end())
				return std::nullopt;
			auto *variant = enum_it->second.variant(path[1]);
			if (variant == nullptr) {
				diagnostics_.error(location,
				                   "unknown enum variant '" + path[1] +
				                       "' for enum '" + path[0] + "'");
				diagnostic_emitted = true;
				return std::nullopt;
			}
			return EnumVariantRef{&enum_it->second, variant};
		}

		if (path.size() != 1)
			return std::nullopt;

		if (expected && is_user_enum(*expected)) {
			auto enum_it = enums_.find(expected->name);
			if (enum_it != enums_.end()) {
				auto *variant = enum_it->second.variant(path[0]);
				if (variant != nullptr)
					return EnumVariantRef{&enum_it->second, variant};
				diagnostics_.error(location,
				                   "unknown enum variant '" + path[0] +
				                       "' for enum '" + expected->name + "'");
				diagnostic_emitted = true;
				return std::nullopt;
			}
		}

		std::optional<EnumVariantRef> match;
		for (const auto &entry : enums_) {
			if (auto *variant = entry.second.variant(path[0])) {
				if (match) {
					diagnostics_.error(location,
					                   "ambiguous enum variant '" + path[0] + "'");
					diagnostic_emitted = true;
					return std::nullopt;
				}
				match = EnumVariantRef{&entry.second, variant};
			}
		}
		return match;
	}

	std::optional<PrimitiveType> check_enum_variant_call(
	    const std::unordered_map<std::string, LocalInfo> &locals,
	    const ast::CallExpr &call, const EnumVariantRef &ref)
	{
		std::size_t expected_count = ref.variant->payload_types.size();
		if (expected_count != call.arguments.size()) {
			diagnostics_.error(
			    call.location,
			    "enum variant '" + ref.variant->name + "' expected " +
			        std::to_string(expected_count) + " arguments but got " +
			        std::to_string(call.arguments.size()));
		}
		for (std::size_t i = 0; i < call.arguments.size(); ++i) {
			std::optional<PrimitiveType> payload_type;
			if (i < expected_count)
				payload_type = ref.variant->payload_types[i];
			auto argument_type = check_expr(locals, *call.arguments[i], payload_type);
			if (payload_type && argument_type &&
			    !types_compatible(*payload_type, *argument_type)) {
				diagnostics_.error(
				    call.arguments[i]->location,
				    "enum payload type mismatch: expected '" +
				        format_type(*payload_type) + "' but got '" +
				        format_type(*argument_type) + "'");
			}
		}
		return enum_type(*ref.enum_info);
	}

	const ast::Module &module_;
	Diagnostics &diagnostics_;
	SemanticOptions options_;
	std::unordered_map<std::string, GlobalInfo> globals_;
	std::unordered_map<std::string, FunctionInfo> functions_;
	std::unordered_set<std::string> reserved_stdlib_function_symbols_;
	std::unordered_set<std::string> reserved_stdlib_module_paths_;
	std::unordered_map<std::string, ImportScope> imports_;
	std::unordered_map<std::string, ModuleInfo> modules_;
	std::unordered_map<std::string, StructInfo> structs_;
	std::unordered_map<std::string, EnumInfo> enums_;
	const std::vector<std::string> *current_module_path_ = nullptr;
	std::optional<PrimitiveType> current_function_return_type_;
	int unsafe_depth_ = 0;
	std::unordered_set<std::string> current_generic_parameters_;
};

} // namespace

SemanticResult::SemanticResult(bool ok) : ok_(ok) {}

bool SemanticResult::ok() const
{
	return ok_;
}

SemanticResult analyze_module(const ast::Module &module, Diagnostics &diagnostics,
                              SemanticOptions options)
{
	Analyzer analyzer(module, diagnostics, options);
	return SemanticResult(analyzer.run());
}

} // namespace rexc
