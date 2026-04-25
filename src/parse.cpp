// ANTLR parser entry point plus Rexc AST construction.
//
// The grammar lives in grammar/Rexc.g4 and CMake generates RexcLexer and
// RexcParser from it. parse.cpp wires those generated classes into Rexc,
// translates ANTLR syntax errors into Rexc Diagnostics, then walks the ANTLR
// parse tree to build the compiler-owned AST from include/rexc/ast.hpp. It
// does not define token kinds, parsing rules, or grammar decisions itself.
#include "rexc/parse.hpp"

#include "RexcLexer.h"
#include "RexcParser.h"

#include <antlr4-runtime.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rexc {
namespace {

class DiagnosticErrorListener final : public antlr4::BaseErrorListener {
public:
	DiagnosticErrorListener(const SourceFile &source, Diagnostics &diagnostics)
		: source_(source), diagnostics_(diagnostics)
	{
	}

	void syntaxError(antlr4::Recognizer *, antlr4::Token *offending_symbol,
	                 std::size_t line, std::size_t char_position_in_line,
	                 const std::string &message, std::exception_ptr) override
	{
		std::size_t offset = source_.text().size();
		if (offending_symbol != nullptr &&
		    offending_symbol->getStartIndex() != static_cast<std::size_t>(-1)) {
			offset = offending_symbol->getStartIndex();
		} else {
			offset = offset_from_line_column(line, char_position_in_line);
		}

		diagnostics_.error(source_.location_at(offset), message);
	}

private:
	std::size_t offset_from_line_column(std::size_t line,
	                                    std::size_t char_position_in_line) const
	{
		if (line == 0)
			return 0;

		std::size_t current_line = 1;
		std::size_t line_start = 0;
		for (std::size_t i = 0; i < source_.text().size() && current_line < line; ++i) {
			if (source_.text()[i] == '\n') {
				++current_line;
				line_start = i + 1;
			}
		}

		return line_start + char_position_in_line;
	}

	const SourceFile &source_;
	Diagnostics &diagnostics_;
};

class AstBuilder {
public:
	AstBuilder(const SourceFile &source, Diagnostics &diagnostics)
		: source_(source), diagnostics_(diagnostics)
	{
	}

	ast::Module build(RexcParser::CompilationUnitContext *context)
	{
		ast::Module module;
		for (auto *item : context->item())
			module.functions.push_back(build_item(item));
		return module;
	}

private:
	SourceLocation location(const antlr4::Token *token) const
	{
		if (token == nullptr)
			return source_.location_at(0);

		auto offset = token->getStartIndex();
		if (offset == static_cast<std::size_t>(-1))
			offset = source_.text().size();
		return source_.location_at(offset);
	}

	SourceLocation location(const antlr4::ParserRuleContext *context) const
	{
		return location(context != nullptr ? context->getStart() : nullptr);
	}

	SourceLocation location(const antlr4::tree::TerminalNode *node) const
	{
		return location(node != nullptr ? node->getSymbol() : nullptr);
	}

	ast::Function build_item(RexcParser::ItemContext *context)
	{
		if (auto *extern_function = context->externFunction())
			return build_extern_function(extern_function);
		return build_function_definition(context->functionDefinition());
	}

	ast::Function build_extern_function(RexcParser::ExternFunctionContext *context)
	{
		ast::Function function = build_signature(context->IDENT(), context->parameterList(),
		                                         context->type(), location(context));
		function.is_extern = true;
		return function;
	}

	ast::Function build_function_definition(RexcParser::FunctionDefinitionContext *context)
	{
		ast::Function function = build_signature(context->IDENT(), context->parameterList(),
		                                         context->type(), location(context));
		function.body = build_block(context->block());
		return function;
	}

	ast::Function build_signature(antlr4::tree::TerminalNode *name,
	                              RexcParser::ParameterListContext *parameters,
	                              RexcParser::TypeContext *return_type,
	                              SourceLocation function_location)
	{
		ast::Function function;
		function.location = std::move(function_location);
		function.name = name->getText();
		if (parameters != nullptr)
			function.parameters = build_parameter_list(parameters);
		function.return_type = build_type(return_type);
		return function;
	}

	std::vector<ast::Parameter> build_parameter_list(RexcParser::ParameterListContext *context)
	{
		std::vector<ast::Parameter> parameters;
		for (auto *parameter : context->parameter())
			parameters.push_back(build_parameter(parameter));
		return parameters;
	}

	ast::Parameter build_parameter(RexcParser::ParameterContext *context)
	{
		auto *name = context->IDENT();
		return ast::Parameter{name->getText(), build_type(context->type()), location(name)};
	}

	ast::TypeName build_type(RexcParser::TypeContext *context)
	{
		auto *primitive = context->primitiveType();
		return ast::TypeName{primitive->getText(), location(primitive)};
	}

	std::vector<std::unique_ptr<ast::Stmt>> build_block(RexcParser::BlockContext *context)
	{
		std::vector<std::unique_ptr<ast::Stmt>> body;
		for (auto *statement : context->statement())
			body.push_back(build_statement(statement));
		return body;
	}

	std::unique_ptr<ast::Stmt> build_statement(RexcParser::StatementContext *context)
	{
		if (auto *let = context->letStatement())
			return build_let_statement(let);
		if (auto *assign = context->assignStatement())
			return build_assign_statement(assign);
		if (auto *ret = context->returnStatement())
			return build_return_statement(ret);
		if (auto *if_statement = context->ifStatement())
			return build_if_statement(if_statement);
		if (auto *while_statement = context->whileStatement())
			return build_while_statement(while_statement);
		if (auto *break_statement = context->breakStatement())
			return std::make_unique<ast::BreakStmt>(location(break_statement));
		if (auto *continue_statement = context->continueStatement())
			return std::make_unique<ast::ContinueStmt>(location(continue_statement));

		diagnostics_.error(location(context), "expected statement");
		return std::make_unique<ast::ReturnStmt>(
		    location(context),
		    std::make_unique<ast::IntegerExpr>(location(context), 0, "0"));
	}

	std::unique_ptr<ast::Stmt> build_let_statement(RexcParser::LetStatementContext *context)
	{
		bool is_mutable = false;
		for (auto *child : context->children) {
			if (child->getText() == "mut") {
				is_mutable = true;
				break;
			}
		}

		auto *name = context->IDENT();
		return std::make_unique<ast::LetStmt>(
		    location(context), is_mutable, name->getText(), build_type(context->type()),
		    build_expression(context->expression()));
	}

	std::unique_ptr<ast::Stmt> build_assign_statement(
	    RexcParser::AssignStatementContext *context)
	{
		auto *name = context->IDENT();
		return std::make_unique<ast::AssignStmt>(
		    location(name), name->getText(), build_expression(context->expression()));
	}

	std::unique_ptr<ast::Stmt> build_return_statement(
	    RexcParser::ReturnStatementContext *context)
	{
		return std::make_unique<ast::ReturnStmt>(
		    location(context), build_expression(context->expression()));
	}

	std::unique_ptr<ast::Stmt> build_if_statement(RexcParser::IfStatementContext *context)
	{
		auto blocks = context->block();
		std::vector<std::unique_ptr<ast::Stmt>> else_body;
		if (blocks.size() > 1)
			else_body = build_block(blocks[1]);

		return std::make_unique<ast::IfStmt>(
		    location(context), build_expression(context->expression()), build_block(blocks[0]),
		    std::move(else_body));
	}

	std::unique_ptr<ast::Stmt> build_while_statement(
	    RexcParser::WhileStatementContext *context)
	{
		return std::make_unique<ast::WhileStmt>(
		    location(context), build_expression(context->expression()),
		    build_block(context->block()));
	}

	std::unique_ptr<ast::Expr> build_expression(RexcParser::ExpressionContext *context)
	{
		return build_comparison(context->comparison());
	}

	std::unique_ptr<ast::Expr> build_comparison(RexcParser::ComparisonContext *context)
	{
		auto operands = context->additive();
		auto lhs = build_additive(operands[0]);
		for (std::size_t i = 1; i < operands.size(); ++i)
			lhs = build_binary(context, 2 * i - 1, std::move(lhs),
			                   build_additive(operands[i]));
		return lhs;
	}

	std::unique_ptr<ast::Expr> build_additive(RexcParser::AdditiveContext *context)
	{
		auto operands = context->multiplicative();
		auto lhs = build_multiplicative(operands[0]);
		for (std::size_t i = 1; i < operands.size(); ++i)
			lhs = build_binary(context, 2 * i - 1, std::move(lhs),
			                   build_multiplicative(operands[i]));
		return lhs;
	}

	std::unique_ptr<ast::Expr> build_multiplicative(
	    RexcParser::MultiplicativeContext *context)
	{
		auto operands = context->unary();
		auto lhs = build_unary(operands[0]);
		for (std::size_t i = 1; i < operands.size(); ++i)
			lhs = build_binary(context, 2 * i - 1, std::move(lhs),
			                   build_unary(operands[i]));
		return lhs;
	}

	std::unique_ptr<ast::Expr> build_binary(
	    antlr4::ParserRuleContext *context, std::size_t operator_child_index,
	    std::unique_ptr<ast::Expr> lhs, std::unique_ptr<ast::Expr> rhs)
	{
		auto *operator_node =
		    dynamic_cast<antlr4::tree::TerminalNode *>(context->children[operator_child_index]);
		std::string op = operator_node != nullptr ? operator_node->getText() : "";
		return std::make_unique<ast::BinaryExpr>(
		    location(operator_node), std::move(op), std::move(lhs), std::move(rhs));
	}

	std::unique_ptr<ast::Expr> build_unary(RexcParser::UnaryContext *context)
	{
		if (auto *primary = context->primary())
			return build_primary(primary);

		auto *operator_node =
		    dynamic_cast<antlr4::tree::TerminalNode *>(context->children.front());
		return std::make_unique<ast::UnaryExpr>(
		    location(operator_node), operator_node != nullptr ? operator_node->getText() : "-",
		    build_unary(context->unary()));
	}

	std::unique_ptr<ast::Expr> build_primary(RexcParser::PrimaryContext *context)
	{
		if (auto *integer = context->INTEGER()) {
			auto text = integer->getText();
			return std::make_unique<ast::IntegerExpr>(
			    location(integer), std::strtoll(text.c_str(), nullptr, 10), text);
		}

		if (auto *boolean = context->BOOL()) {
			return std::make_unique<ast::BoolExpr>(
			    location(boolean), boolean->getText() == "true");
		}

		if (auto *character = context->CHAR()) {
			auto decoded = decode_quoted_literal(character->getText());
			char32_t value = decoded.empty()
			                     ? U'\0'
			                     : static_cast<unsigned char>(decoded.front());
			return std::make_unique<ast::CharExpr>(location(character), value);
		}

		if (auto *string = context->STRING()) {
			return std::make_unique<ast::StringExpr>(
			    location(string), decode_quoted_literal(string->getText()));
		}

		if (auto *call = context->callExpression())
			return build_call_expression(call);

		if (auto *name = context->IDENT())
			return std::make_unique<ast::NameExpr>(location(name), name->getText());

		return build_expression(context->expression());
	}

	std::unique_ptr<ast::Expr> build_call_expression(
	    RexcParser::CallExpressionContext *context)
	{
		auto *name = context->IDENT();
		auto call = std::make_unique<ast::CallExpr>(location(name), name->getText());
		if (auto *arguments = context->argumentList()) {
			for (auto *argument : arguments->expression())
				call->arguments.push_back(build_expression(argument));
		}
		return call;
	}

	std::string decode_quoted_literal(const std::string &literal) const
	{
		if (literal.size() < 2)
			return "";

		std::string decoded;
		for (std::size_t i = 1; i + 1 < literal.size(); ++i) {
			char ch = literal[i];
			if (ch != '\\') {
				decoded.push_back(ch);
				continue;
			}

			if (++i + 1 > literal.size())
				break;
			switch (literal[i]) {
			case 'n':
				decoded.push_back('\n');
				break;
			case 'r':
				decoded.push_back('\r');
				break;
			case 't':
				decoded.push_back('\t');
				break;
			case '\'':
				decoded.push_back('\'');
				break;
			case '"':
				decoded.push_back('"');
				break;
			case '\\':
				decoded.push_back('\\');
				break;
			default:
				decoded.push_back(literal[i]);
				break;
			}
		}
		return decoded;
	}

	const SourceFile &source_;
	Diagnostics &diagnostics_;
};

} // namespace

ParseResult::ParseResult(bool ok, ast::Module module)
	: ok_(ok), module_(std::move(module))
{
}

bool ParseResult::ok() const
{
	return ok_;
}

const ast::Module &ParseResult::module() const
{
	return module_;
}

ast::Module ParseResult::take_module()
{
	return std::move(module_);
}

ParseResult parse_source(const SourceFile &source, Diagnostics &diagnostics)
{
	antlr4::ANTLRInputStream input(source.text());
	RexcLexer lexer(&input);
	antlr4::CommonTokenStream tokens(&lexer);
	RexcParser parser(&tokens);
	DiagnosticErrorListener error_listener(source, diagnostics);

	lexer.removeErrorListeners();
	parser.removeErrorListeners();
	lexer.addErrorListener(&error_listener);
	parser.addErrorListener(&error_listener);

	auto *tree = parser.compilationUnit();
	if (diagnostics.has_errors())
		return ParseResult(false, {});

	AstBuilder builder(source, diagnostics);
	auto module = builder.build(tree);
	return ParseResult(!diagnostics.has_errors(), std::move(module));
}

} // namespace rexc
