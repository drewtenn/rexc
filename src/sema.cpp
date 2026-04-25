#include "rexc/sema.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace rexc {
namespace {

struct FunctionInfo {
	const ast::Function *function = nullptr;
};

class Analyzer {
public:
	Analyzer(const ast::Module &module, Diagnostics &diagnostics)
		: module_(module), diagnostics_(diagnostics)
	{
	}

	bool run()
	{
		build_function_table();

		for (const auto &function : module_.functions) {
			if (!function.is_extern)
				analyze_function(function);
		}

		return !diagnostics_.has_errors();
	}

private:
	void build_function_table()
	{
		for (const auto &function : module_.functions) {
			if (functions_.find(function.name) != functions_.end()) {
				diagnostics_.error(function.location, "duplicate function '" + function.name + "'");
				continue;
			}
			functions_[function.name] = FunctionInfo{&function};
		}
	}

	void analyze_function(const ast::Function &function)
	{
		std::unordered_set<std::string> locals;

		for (const auto &parameter : function.parameters) {
			if (!locals.insert(parameter.name).second)
				diagnostics_.error(parameter.location, "duplicate local '" + parameter.name + "'");
			check_type(parameter.type);
		}

		check_type(function.return_type);

		for (const auto &statement : function.body)
			analyze_statement(function, locals, *statement);
	}

	void analyze_statement(const ast::Function &function, std::unordered_set<std::string> &locals,
	                       const ast::Stmt &statement)
	{
		if (statement.kind == ast::Stmt::Kind::Let) {
			const auto &let = static_cast<const ast::LetStmt &>(statement);
			if (!locals.insert(let.name).second)
				diagnostics_.error(let.location, "duplicate local '" + let.name + "'");
			check_type(let.type);
			check_expr(locals, *let.initializer);
			return;
		}

		const auto &ret = static_cast<const ast::ReturnStmt &>(statement);
		std::string value_type = check_expr(locals, *ret.value);
		if (value_type != function.return_type.name) {
			diagnostics_.error(ret.location, "return type mismatch: expected '" +
			                   function.return_type.name + "' but got '" + value_type + "'");
		}
	}

	std::string check_expr(const std::unordered_set<std::string> &locals, const ast::Expr &expr)
	{
		switch (expr.kind) {
		case ast::Expr::Kind::Integer:
			return "i32";
		case ast::Expr::Kind::Bool:
		case ast::Expr::Kind::Char:
		case ast::Expr::Kind::String:
			diagnostics_.error(expr.location,
			                   "literal type is not supported by semantic analysis yet");
			return "i32";
		case ast::Expr::Kind::Name: {
			const auto &name = static_cast<const ast::NameExpr &>(expr);
			if (locals.find(name.name) == locals.end())
				diagnostics_.error(name.location, "unknown name '" + name.name + "'");
			return "i32";
		}
		case ast::Expr::Kind::Binary: {
			const auto &binary = static_cast<const ast::BinaryExpr &>(expr);
			check_expr(locals, *binary.lhs);
			check_expr(locals, *binary.rhs);
			return "i32";
		}
		case ast::Expr::Kind::Unary: {
			const auto &unary = static_cast<const ast::UnaryExpr &>(expr);
			return check_expr(locals, *unary.operand);
		}
		case ast::Expr::Kind::Call: {
			const auto &call = static_cast<const ast::CallExpr &>(expr);
			auto it = functions_.find(call.callee);
			if (it == functions_.end()) {
				diagnostics_.error(call.location, "unknown function '" + call.callee + "'");
			} else {
				std::size_t expected = it->second.function->parameters.size();
				if (expected != call.arguments.size()) {
					diagnostics_.error(call.location, "function '" + call.callee + "' expected " +
					                   std::to_string(expected) + " arguments but got " +
					                   std::to_string(call.arguments.size()));
				}
			}
			for (const auto &argument : call.arguments)
				check_expr(locals, *argument);
			return "i32";
		}
		default:
			break;
		}

		return "i32";
	}

	void check_type(const ast::TypeName &type)
	{
		if (type.name != "i32")
			diagnostics_.error(type.location, "unknown type '" + type.name + "'");
	}

	const ast::Module &module_;
	Diagnostics &diagnostics_;
	std::unordered_map<std::string, FunctionInfo> functions_;
};

} // namespace

SemanticResult::SemanticResult(bool ok) : ok_(ok) {}

bool SemanticResult::ok() const
{
	return ok_;
}

SemanticResult analyze_module(const ast::Module &module, Diagnostics &diagnostics)
{
	Analyzer analyzer(module, diagnostics);
	return SemanticResult(analyzer.run());
}

} // namespace rexc
