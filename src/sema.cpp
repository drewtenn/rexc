// Semantic analysis for the parsed Rexc AST.
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

struct FunctionInfo {
	SourceLocation location;
	PrimitiveType return_type = PrimitiveType{PrimitiveKind::SignedInteger, 32};
	std::vector<PrimitiveType> parameter_types;
	std::vector<std::string> module_path;
	ast::Visibility visibility = ast::Visibility::Private;
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

	void build_module_table()
	{
		if (include_stdlib_symbols()) {
			for (const auto &function : stdlib::stdlib_functions()) {
				if (auto path = stdlib_path_for_symbol(function.name))
					note_item_path(*path, true);
			}
		}

		for (const auto &module : module_.modules) {
			note_module_path(module.module_path);
			std::string key = canonical_path(module.module_path);
			if (modules_.find(key) != modules_.end() &&
			    !modules_[key].location.file.empty()) {
				diagnostics_.error(module.location, "duplicate module '" + key + "'");
				continue;
			}
			modules_[key] = ModuleInfo{module.location, module.module_path,
			                           module.visibility};
		}
	}

	void build_static_table()
	{
		for (const auto &buffer : module_.static_buffers) {
			std::string key = canonical_item_path(buffer.module_path, buffer.name);
			if (globals_.find(key) != globals_.end()) {
				diagnostics_.error(buffer.location, "duplicate static '" + key + "'");
				continue;
			}
			note_module_path(buffer.module_path);

			PrimitiveType element_type = check_type(buffer.element_type);
			if (element_type != u8_type()) {
				diagnostics_.error(buffer.element_type.location,
				                   "static buffers currently require u8 elements");
			}
			if (!buffer.is_mutable)
				diagnostics_.error(buffer.location, "static buffer must be mutable");

			auto length = parse_decimal_magnitude(buffer.length_literal);
			if (!length || *length == 0)
				diagnostics_.error(buffer.location, "static buffer length must be greater than zero");

			globals_[key] = GlobalInfo{PrimitiveType{PrimitiveKind::Str},
			                           buffer.is_mutable, buffer.module_path,
			                           buffer.visibility};
		}

		for (const auto &scalar : module_.static_scalars) {
			std::string key = canonical_item_path(scalar.module_path, scalar.name);
			if (globals_.find(key) != globals_.end()) {
				diagnostics_.error(scalar.location, "duplicate static '" + key + "'");
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

	void build_function_table()
	{
		if (include_stdlib_symbols()) {
			for (const auto &function : stdlib::stdlib_functions()) {
				reserved_stdlib_function_symbols_.insert(function.name);
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
			if (functions_.find(key) != functions_.end() ||
			    (function.module_path.empty() &&
			     reserved_stdlib_function_symbols_.find(function.name) !=
			         reserved_stdlib_function_symbols_.end()) ||
			    globals_.find(key) != globals_.end()) {
				diagnostics_.error(function.location, "duplicate function '" + key + "'");
				continue;
			}
			note_module_path(function.module_path);
			FunctionInfo info;
			info.location = function.location;
			info.return_type = check_type(function.return_type);
			for (const auto &parameter : function.parameters)
				info.parameter_types.push_back(check_type(parameter.type));
			info.module_path = function.module_path;
			info.visibility = function.visibility;
			functions_[key] = std::move(info);
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

		for (const auto &parameter : function.parameters) {
			PrimitiveType parameter_type = check_type(parameter.type);
			if (!locals.emplace(parameter.name, LocalInfo{parameter_type, false}).second)
				diagnostics_.error(parameter.location, "duplicate local '" + parameter.name + "'");
		}

		PrimitiveType return_type = check_type(function.return_type);

		for (const auto &statement : function.body)
			analyze_statement(return_type, locals, *statement, 0);
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
			if (initializer_type && *initializer_type != let_type) {
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
				if (value_type && *value_type != global->type) {
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
			if (value_type && *value_type != it->second.type) {
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

			auto value_type = check_expr(locals, *assign.value, *target_pointee);
			if (value_type && *value_type != *target_pointee) {
				diagnostics_.error(assign.location, "assignment type mismatch: expected '" +
				                   format_type(*target_pointee) + "' but got '" +
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

		if (statement.kind == ast::Stmt::Kind::Expr) {
			const auto &expr_statement = static_cast<const ast::ExprStmt &>(statement);
			check_expr(locals, *expr_statement.value);
			return;
		}

		const auto &ret = static_cast<const ast::ReturnStmt &>(statement);
		auto value_type = check_expr(locals, *ret.value, function_return_type);
		if (value_type && *value_type != function_return_type) {
			diagnostics_.error(ret.location, "return type mismatch: expected '" +
			                   format_type(function_return_type) + "' but got '" +
			                   format_type(*value_type) + "'");
		}
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
			auto function = resolve_function(call.callee_path, call.location);
			if (function == nullptr) {
				if (!has_invalid_import_prefix(call.callee_path))
					diagnostics_.error(call.location,
					                   "unknown function '" + call.callee + "'");
			} else {
				std::size_t expected_count = function->parameter_types.size();
				if (expected_count != call.arguments.size()) {
					diagnostics_.error(call.location, "function '" + call.callee + "' expected " +
					                   std::to_string(expected_count) + " arguments but got " +
					                   std::to_string(call.arguments.size()));
				}
				for (std::size_t i = 0; i < call.arguments.size(); ++i) {
					std::optional<PrimitiveType> parameter_type;
					if (i < expected_count)
						parameter_type = function->parameter_types[i];
					auto argument_type = check_expr(locals, *call.arguments[i], parameter_type);
					if (parameter_type && argument_type && *argument_type != *parameter_type) {
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
		default:
			break;
		}

		return i32_type();
	}

	std::optional<PrimitiveType> check_unary_expr(
		const std::unordered_map<std::string, LocalInfo> &locals, const ast::UnaryExpr &unary,
		std::optional<PrimitiveType> expected)
	{
		if (unary.op == "!")
			return check_logical_not_expr(locals, unary);
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
		if (source == pointer_to(u8_type()) && target.kind == PrimitiveKind::Str)
			return true;
		if (is_pointer(source) && is_pointer(target))
			return true;
		if (is_integer(source) && is_integer(target))
			return true;
		if (source.kind == PrimitiveKind::Bool && is_integer(target))
			return true;
		PrimitiveType char_scalar_type{PrimitiveKind::UnsignedInteger, 32};
		if (source.kind == PrimitiveKind::Char && target == char_scalar_type)
			return true;
		return false;
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
		auto primitive_type = parse_primitive_type(type.name);
		if (!primitive_type) {
			diagnostics_.error(type.location, "unknown type '" + type.name + "'");
			return i32_type();
		}
		return *primitive_type;
	}

	const ast::Module &module_;
	Diagnostics &diagnostics_;
	SemanticOptions options_;
	std::unordered_map<std::string, GlobalInfo> globals_;
	std::unordered_map<std::string, FunctionInfo> functions_;
	std::unordered_set<std::string> reserved_stdlib_function_symbols_;
	std::unordered_map<std::string, ImportScope> imports_;
	std::unordered_map<std::string, ModuleInfo> modules_;
	const std::vector<std::string> *current_module_path_ = nullptr;
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
