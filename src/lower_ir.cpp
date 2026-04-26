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

ir::Type lower_type(const ast::TypeName &type)
{
	auto primitive_type = parse_primitive_type(type.name);
	if (!primitive_type)
		throw std::runtime_error("unknown primitive type in IR lowering: " + type.name);
	return *primitive_type;
}

struct FunctionInfo {
	ir::Type return_type = i32_type();
	std::vector<ir::Type> parameter_types;
	std::string symbol_name;
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
		build_static_table();
		build_function_table();
		build_import_table();

		ir::Module lowered;
		for (const auto &buffer : module_.static_buffers)
			lowered.static_buffers.push_back(lower_static_buffer(buffer));
		for (const auto &scalar : module_.static_scalars)
			lowered.static_scalars.push_back(lower_static_scalar(scalar));
		for (const auto &function : module_.functions)
			lowered.functions.push_back(lower_function(function));
		return lowered;
	}

private:
	using Locals = std::unordered_map<std::string, ir::Type>;
	using ImportScope = std::unordered_map<std::string, ImportInfo>;

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
		for (const auto &buffer : module_.static_buffers)
			globals_[canonical_item_path(buffer.module_path, buffer.name)] =
				GlobalInfo{PrimitiveType{PrimitiveKind::Str},
				           symbol_item_path(buffer.module_path, buffer.name)};
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
			info.return_type = lower_type(function.return_type);
			for (const auto &parameter : function.parameters)
				info.parameter_types.push_back(lower_type(parameter.type));
			info.symbol_name = symbol_item_path(function.module_path, function.name);
			functions_[canonical_item_path(function.module_path, function.name)] =
				std::move(info);
			note_module_path(function.module_path);
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
			auto function = resolve_function(call_expr.callee_path);
			if (function == nullptr)
				throw std::runtime_error("unknown function in IR lowering: " +
				                         call_expr.callee);

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
		}

		throw std::runtime_error("unexpected expression in IR lowering");
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

	std::vector<std::unique_ptr<ir::Statement>> lower_statements(
		const std::vector<std::unique_ptr<ast::Stmt>> &statements,
		ir::Type function_return_type, Locals locals)
	{
		std::vector<std::unique_ptr<ir::Statement>> lowered;
		for (const auto &statement : statements)
			lowered.push_back(lower_statement(*statement, function_return_type, locals));
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

		if (statement.kind == ast::Stmt::Kind::If) {
			const auto &if_stmt = static_cast<const ast::IfStmt &>(statement);
			auto condition = lower_expr(*if_stmt.condition, locals, bool_type());
			return std::make_unique<ir::IfStatement>(
				std::move(condition),
				lower_statements(if_stmt.then_body, function_return_type, locals),
				lower_statements(if_stmt.else_body, function_return_type, locals));
		}

		if (statement.kind == ast::Stmt::Kind::While) {
			const auto &while_stmt = static_cast<const ast::WhileStmt &>(statement);
			auto condition = lower_expr(*while_stmt.condition, locals, bool_type());
			return std::make_unique<ir::WhileStatement>(
				std::move(condition),
				lower_statements(while_stmt.body, function_return_type, locals));
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

		for (const auto &statement : function.body) {
			lowered.body.push_back(
				lower_statement(*statement, lowered.return_type, locals));
		}

		current_module_path_ = nullptr;
		return lowered;
	}

	ir::StaticBuffer lower_static_buffer(const ast::StaticBuffer &buffer)
	{
		ir::StaticBuffer lowered;
		lowered.name = symbol_item_path(buffer.module_path, buffer.name);
		lowered.element_type = lower_type(buffer.element_type);
		lowered.length = static_cast<std::size_t>(std::stoull(buffer.length_literal));
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
	const std::vector<std::string> *current_module_path_ = nullptr;
};

} // namespace

ir::Module lower_to_ir(const ast::Module &module, LowerOptions options)
{
	return Lowerer(module, options).run();
}

} // namespace rexc
