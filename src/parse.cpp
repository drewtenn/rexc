// ANTLR parser entry point plus Rexy AST construction.
//
// The grammar lives in grammar/Rexy.g4 and CMake generates RexyLexer and
// RexyParser from it. parse.cpp wires those generated classes into rexc,
// translates ANTLR syntax errors into rexc Diagnostics, then walks the ANTLR
// parse tree to build the compiler-owned AST from include/rexc/ast.hpp. It
// does not define token kinds, parsing rules, or grammar decisions itself.
#include "rexc/parse.hpp"

#include "RexyLexer.h"
#include "RexyParser.h"

#include <antlr4-runtime.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
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
	AstBuilder(const SourceFile &source, Diagnostics &diagnostics, ParseOptions options)
		: source_(source), diagnostics_(diagnostics), options_(std::move(options))
	{
	}

	ast::Module build(RexyParser::CompilationUnitContext *context)
	{
		ast::Module module;
		for (auto *item : context->item())
			build_item(module, item, options_.module_path);
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

	ast::Visibility visibility(const antlr4::ParserRuleContext *context) const
	{
		for (auto *child : context->children) {
			if (child->getText() == "pub")
				return ast::Visibility::Public;
		}
		return ast::Visibility::Private;
	}

	bool has_child_text(const antlr4::ParserRuleContext *context,
	                    const std::string &text) const
	{
		for (auto *child : context->children) {
			if (child->getText() == text)
				return true;
		}
		return false;
	}

	void build_item(ast::Module &module, RexyParser::ItemContext *context,
	                const std::vector<std::string> &module_path)
	{
		if (auto *static_buffer = context->staticBuffer()) {
			module.static_buffers.push_back(build_static_buffer(static_buffer, module_path));
			return;
		}
		if (auto *static_scalar = context->staticScalar()) {
			module.static_scalars.push_back(build_static_scalar(static_scalar, module_path));
			return;
		}
		if (auto *struct_declaration = context->structDeclaration()) {
			module.structs.push_back(build_struct_declaration(struct_declaration, module_path));
			return;
		}
		if (auto *enum_declaration = context->enumDeclaration()) {
			module.enums.push_back(build_enum_declaration(enum_declaration, module_path));
			return;
		}
		if (auto *extern_function = context->externFunction()) {
			module.functions.push_back(build_extern_function(extern_function, module_path));
			return;
		}
		if (auto *function_definition = context->functionDefinition()) {
			module.functions.push_back(build_function_definition(function_definition, module_path));
			return;
		}
		if (auto *module_declaration = context->moduleDeclaration()) {
			auto nested_path = module_path;
			nested_path.push_back(module_declaration->IDENT()->getText());
			bool is_file_backed = has_child_text(module_declaration, ";");
			module.modules.push_back(ast::ModuleDecl{
			    visibility(module_declaration), nested_path, is_file_backed,
			    location(module_declaration)});
			if (!is_file_backed) {
				for (auto *nested_item : module_declaration->item())
					build_item(module, nested_item, nested_path);
			}
			return;
		}
		if (auto *use_declaration = context->useDeclaration()) {
			module.uses.push_back(ast::UseDecl{
			    module_path, build_qualified_name(use_declaration->qualifiedName()),
			    location(use_declaration)});
			return;
		}
	}

	ast::StaticBuffer build_static_buffer(RexyParser::StaticBufferContext *context,
	                                      const std::vector<std::string> &module_path)
	{
		bool is_mutable = false;
		for (auto *child : context->children) {
			if (child->getText() == "mut") {
				is_mutable = true;
				break;
			}
		}

		ast::StaticBuffer buffer;
		buffer.is_mutable = is_mutable;
		buffer.visibility = visibility(context);
		buffer.name = context->IDENT()->getText();
		buffer.element_type = ast::TypeName{context->primitiveType()->getText(),
		                                    location(context->primitiveType())};
		buffer.length_literal = context->INTEGER()->getText();
		if (auto *initializer = context->staticArrayInitializer())
			buffer.initializers = build_static_array_initializer(initializer);
		buffer.location = location(context);
		buffer.module_path = module_path;
		return buffer;
	}

	std::vector<ast::StaticBuffer::Initializer> build_static_array_initializer(
	    RexyParser::StaticArrayInitializerContext *context)
	{
		std::vector<ast::StaticBuffer::Initializer> initializers;
		for (auto *element : context->staticArrayElement())
			initializers.push_back(build_static_array_element(element));
		return initializers;
	}

	ast::StaticBuffer::Initializer build_static_array_element(
	    RexyParser::StaticArrayElementContext *context)
	{
		ast::StaticBuffer::Initializer initializer;
		initializer.location = location(context);
		if (auto *integer = context->INTEGER()) {
			initializer.kind = ast::StaticBuffer::Initializer::Kind::Integer;
			initializer.literal = integer->getText();
			initializer.is_negative =
			    !context->children.empty() && context->children.front()->getText() == "-";
			return initializer;
		}
		if (auto *boolean = context->BOOL()) {
			initializer.kind = ast::StaticBuffer::Initializer::Kind::Bool;
			initializer.bool_value = boolean->getText() == "true";
			initializer.literal = boolean->getText();
			return initializer;
		}
		if (auto *character = context->CHAR()) {
			auto decoded = decode_quoted_literal(character->getText());
			initializer.kind = ast::StaticBuffer::Initializer::Kind::Char;
			initializer.char_value = decoded.empty()
			                             ? U'\0'
			                             : static_cast<unsigned char>(decoded.front());
			initializer.literal = character->getText();
			return initializer;
		}

		auto *string = context->STRING();
		initializer.kind = ast::StaticBuffer::Initializer::Kind::String;
		initializer.literal = decode_quoted_literal(string->getText());
		return initializer;
	}

	ast::StaticScalar build_static_scalar(RexyParser::StaticScalarContext *context,
	                                      const std::vector<std::string> &module_path)
	{
		bool is_mutable = false;
		for (auto *child : context->children) {
			if (child->getText() == "mut") {
				is_mutable = true;
				break;
			}
		}

		ast::StaticScalar scalar;
		scalar.is_mutable = is_mutable;
		scalar.visibility = visibility(context);
		scalar.name = context->IDENT()->getText();
		scalar.type = ast::TypeName{context->primitiveType()->getText(),
		                            location(context->primitiveType())};
		scalar.initializer_literal = context->INTEGER()->getText();
		scalar.location = location(context);
		scalar.module_path = module_path;
		return scalar;
	}

	ast::StructDecl build_struct_declaration(RexyParser::StructDeclarationContext *context,
	                                         const std::vector<std::string> &module_path)
	{
		ast::StructDecl decl;
		decl.visibility = visibility(context);
		decl.name = context->IDENT()->getText();
		decl.location = location(context);
		decl.module_path = module_path;
		if (auto *generics = context->genericParameters())
			decl.generic_parameters = build_generic_parameters(generics);
		for (auto *field : context->structField())
			decl.fields.push_back(build_struct_field(field));
		return decl;
	}

	std::vector<std::string> build_generic_parameters(
		RexyParser::GenericParametersContext *context)
	{
		std::vector<std::string> names;
		for (auto *ident : context->IDENT())
			names.push_back(ident->getText());
		return names;
	}

	ast::StructField build_struct_field(RexyParser::StructFieldContext *context)
	{
		auto *name = context->IDENT();
		return ast::StructField{name->getText(), build_type(context->type()), location(name)};
	}

	ast::EnumDecl build_enum_declaration(RexyParser::EnumDeclarationContext *context,
	                                     const std::vector<std::string> &module_path)
	{
		ast::EnumDecl decl;
		decl.visibility = visibility(context);
		decl.name = context->IDENT()->getText();
		decl.location = location(context);
		decl.module_path = module_path;
		for (auto *variant : context->enumVariant())
			decl.variants.push_back(build_enum_variant(variant));
		return decl;
	}

	ast::EnumVariant build_enum_variant(RexyParser::EnumVariantContext *context)
	{
		auto *name = context->IDENT();
		ast::EnumVariant variant;
		variant.name = name->getText();
		variant.location = location(name);
		if (auto *payload = context->enumPayloadTypeList()) {
			for (auto *type : payload->type())
				variant.payload_types.push_back(build_type(type));
		}
		return variant;
	}

	ast::Function build_extern_function(RexyParser::ExternFunctionContext *context,
	                                    const std::vector<std::string> &module_path)
	{
		ast::Function function = build_signature(context->IDENT(), context->parameterList(),
		                                         context->type(), location(context));
		function.is_extern = true;
		function.visibility = visibility(context);
		function.module_path = module_path;
		return function;
	}

	ast::Function build_function_definition(RexyParser::FunctionDefinitionContext *context,
	                                        const std::vector<std::string> &module_path)
	{
		ast::Function function = build_signature(context->IDENT(), context->parameterList(),
		                                         context->type(), location(context));
		function.visibility = visibility(context);
		function.is_unsafe = has_unsafe_modifier(context);
		if (auto *generics = context->genericParameters())
			function.generic_parameters = build_generic_parameters(generics);
		function.module_path = module_path;
		function.body = build_block(context->block());
		return function;
	}

	bool has_unsafe_modifier(RexyParser::FunctionDefinitionContext *context)
	{
		// 'pub'? 'unsafe'? 'fn' IDENT — scan leading terminals up to 'fn'.
		for (auto *child : context->children) {
			std::string text = child->getText();
			if (text == "fn")
				return false;
			if (text == "unsafe")
				return true;
		}
		return false;
	}

	std::unique_ptr<ast::Stmt> build_unsafe_block(RexyParser::UnsafeBlockContext *context)
	{
		std::vector<std::unique_ptr<ast::Stmt>> body;
		for (auto *statement : context->statement())
			body.push_back(build_statement(statement));
		return std::make_unique<ast::UnsafeBlockStmt>(location(context), std::move(body));
	}

	ast::Function build_signature(antlr4::tree::TerminalNode *name,
	                              RexyParser::ParameterListContext *parameters,
	                              RexyParser::TypeContext *return_type,
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

	std::vector<ast::Parameter> build_parameter_list(RexyParser::ParameterListContext *context)
	{
		std::vector<ast::Parameter> parameters;
		for (auto *parameter : context->parameter())
			parameters.push_back(build_parameter(parameter));
		return parameters;
	}

	ast::Parameter build_parameter(RexyParser::ParameterContext *context)
	{
		auto *name = context->IDENT();
		return ast::Parameter{name->getText(), build_type(context->type()), location(name)};
	}

	ast::TypeName build_type(RexyParser::TypeContext *context)
	{
		return ast::TypeName{context->getText(), location(context)};
	}

	std::vector<std::unique_ptr<ast::Stmt>> build_block(RexyParser::BlockContext *context)
	{
		std::vector<std::unique_ptr<ast::Stmt>> body;
		for (auto *statement : context->statement())
			body.push_back(build_statement(statement));
		return body;
	}

	std::unique_ptr<ast::Stmt> build_statement(RexyParser::StatementContext *context)
	{
		if (auto *let = context->letStatement())
			return build_let_statement(let);
		if (auto *assign = context->assignStatement())
			return build_assign_statement(assign);
		if (auto *indirect_assign = context->indirectAssignStatement())
			return build_indirect_assign_statement(indirect_assign);
		if (auto *field_assign = context->fieldAssignStatement())
			return build_field_assign_statement(field_assign);
		if (auto *inc_dec = context->incDecStatement())
			return build_inc_dec_statement(inc_dec);
		if (auto *call_statement = context->callStatement())
			return build_call_statement(call_statement);
		if (auto *ret = context->returnStatement())
			return build_return_statement(ret);
		if (auto *if_statement = context->ifStatement())
			return build_if_statement(if_statement);
		if (auto *match_statement = context->matchStatement())
			return build_match_statement(match_statement);
		if (auto *while_statement = context->whileStatement())
			return build_while_statement(while_statement);
		if (auto *for_statement = context->forStatement())
			return build_for_statement(for_statement);
		if (auto *break_statement = context->breakStatement())
			return std::make_unique<ast::BreakStmt>(location(break_statement));
		if (auto *continue_statement = context->continueStatement())
			return std::make_unique<ast::ContinueStmt>(location(continue_statement));
		if (auto *unsafe_block = context->unsafeBlock())
			return build_unsafe_block(unsafe_block);

		diagnostics_.error(location(context), "expected statement");
		return std::make_unique<ast::ReturnStmt>(
		    location(context),
		    std::make_unique<ast::IntegerExpr>(location(context), 0, "0"));
	}

	std::unique_ptr<ast::Stmt> build_let_statement(RexyParser::LetStatementContext *context)
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
	    RexyParser::AssignStatementContext *context)
	{
		auto *name = context->IDENT();
		return std::make_unique<ast::AssignStmt>(
		    location(name), name->getText(), build_expression(context->expression()));
	}

	std::unique_ptr<ast::Stmt> build_indirect_assign_statement(
	    RexyParser::IndirectAssignStatementContext *context)
	{
		return std::make_unique<ast::IndirectAssignStmt>(
		    location(context), build_expression(context->expression(0)),
		    build_expression(context->expression(1)));
	}

	std::unique_ptr<ast::Stmt> build_field_assign_statement(
	    RexyParser::FieldAssignStatementContext *context)
	{
		// Grammar: '(' '*' expression ')' '.' IDENT '=' expression ';'
		// expression(0) is the pointer expr; expression(1) is the rhs value.
		return std::make_unique<ast::FieldAssignStmt>(
		    location(context), build_expression(context->expression(0)),
		    context->IDENT()->getText(), build_expression(context->expression(1)));
	}

	std::unique_ptr<ast::Stmt> build_inc_dec_statement(
	    RexyParser::IncDecStatementContext *context)
	{
		return std::make_unique<ast::ExprStmt>(
		    location(context), build_increment_expression(context->incrementExpression()));
	}

	std::unique_ptr<ast::Stmt> build_call_statement(RexyParser::CallStatementContext *context)
	{
		return std::make_unique<ast::ExprStmt>(
		    location(context), build_call_expression(context->callExpression()));
	}

	std::unique_ptr<ast::Stmt> build_return_statement(
	    RexyParser::ReturnStatementContext *context)
	{
		return std::make_unique<ast::ReturnStmt>(
		    location(context), build_expression(context->expression()));
	}

	std::unique_ptr<ast::Stmt> build_if_statement(RexyParser::IfStatementContext *context)
	{
		auto blocks = context->block();
		std::vector<std::unique_ptr<ast::Stmt>> else_body;
		if (blocks.size() > 1)
			else_body = build_block(blocks[1]);
		else if (auto *else_if = context->ifStatement())
			else_body.push_back(build_if_statement(else_if));

		return std::make_unique<ast::IfStmt>(
		    location(context), build_expression(context->expression()), build_block(blocks[0]),
		    std::move(else_body));
	}

	std::unique_ptr<ast::Stmt> build_match_statement(
	    RexyParser::MatchStatementContext *context)
	{
		std::vector<ast::MatchArm> arms;
		for (auto *arm : context->matchArm())
			arms.push_back(build_match_arm(arm));
		return std::make_unique<ast::MatchStmt>(
		    location(context), build_expression(context->expression()), std::move(arms));
	}

	ast::MatchArm build_match_arm(RexyParser::MatchArmContext *context)
	{
		std::vector<ast::MatchPattern> patterns;
		for (auto *pattern : context->matchPattern())
			patterns.push_back(build_match_pattern(pattern));
		return ast::MatchArm{
		    std::move(patterns),
		    build_block(context->block()),
		};
	}

	ast::MatchPattern build_match_pattern(RexyParser::MatchPatternContext *context)
	{
		ast::MatchPattern pattern;
		pattern.location = location(context);
		if (auto *integer = context->INTEGER()) {
			pattern.kind = ast::MatchPattern::Kind::Integer;
			pattern.literal = integer->getText();
			pattern.is_negative =
			    !context->children.empty() && context->children.front()->getText() == "-";
			return pattern;
		}
		if (auto *boolean = context->BOOL()) {
			pattern.kind = ast::MatchPattern::Kind::Bool;
			pattern.bool_value = boolean->getText() == "true";
			pattern.literal = boolean->getText();
			return pattern;
		}
		if (auto *character = context->CHAR()) {
			auto decoded = decode_quoted_literal(character->getText());
			pattern.kind = ast::MatchPattern::Kind::Char;
			pattern.char_value = decoded.empty()
			                         ? U'\0'
			                         : static_cast<unsigned char>(decoded.front());
			pattern.literal = character->getText();
			return pattern;
		}
		if (auto *path = context->qualifiedName()) {
			pattern.kind = ast::MatchPattern::Kind::Variant;
			pattern.path = build_qualified_name(path);
			pattern.bindings = build_pattern_bindings(context->patternBindingList());
			return pattern;
		}
		if (auto *identifier = context->IDENT()) {
			pattern.kind = ast::MatchPattern::Kind::Struct;
			pattern.path.push_back(identifier->getText());
			pattern.bindings = build_pattern_bindings(context->patternBindingList());
			return pattern;
		}
		return pattern;
	}

	std::vector<std::string> build_pattern_bindings(
	    RexyParser::PatternBindingListContext *context)
	{
		std::vector<std::string> bindings;
		if (context == nullptr)
			return bindings;
		for (auto *binding : context->patternBinding())
			bindings.push_back(binding->getText());
		return bindings;
	}

	std::unique_ptr<ast::Stmt> build_while_statement(
	    RexyParser::WhileStatementContext *context)
	{
		return std::make_unique<ast::WhileStmt>(
		    location(context), build_expression(context->expression()),
		    build_block(context->block()));
	}

	std::unique_ptr<ast::Stmt> build_for_statement(RexyParser::ForStatementContext *context)
	{
		return std::make_unique<ast::ForStmt>(
		    location(context), build_for_initializer(context->forInitializer()),
		    build_expression(context->expression()),
		    build_for_increment(context->forIncrement()), build_block(context->block()));
	}

	std::unique_ptr<ast::Stmt> build_for_initializer(
	    RexyParser::ForInitializerContext *context)
	{
		if (auto *let = context->letStatement())
			return build_let_statement(let);
		return build_assign_statement(context->assignStatement());
	}

	std::unique_ptr<ast::Stmt> build_for_increment(RexyParser::ForIncrementContext *context)
	{
		if (auto *name = context->IDENT()) {
			return std::make_unique<ast::AssignStmt>(
			    location(name), name->getText(), build_expression(context->expression(0)));
		}
		if (auto *increment = context->incrementExpression()) {
			return std::make_unique<ast::ExprStmt>(
			    location(increment), build_increment_expression(increment));
		}

		return std::make_unique<ast::IndirectAssignStmt>(
		    location(context), build_expression(context->expression(0)),
		    build_expression(context->expression(1)));
	}

	std::unique_ptr<ast::Expr> build_expression(RexyParser::ExpressionContext *context)
	{
		return build_logical_or(context->logicalOr());
	}

	std::unique_ptr<ast::Expr> build_logical_or(RexyParser::LogicalOrContext *context)
	{
		auto operands = context->logicalAnd();
		auto lhs = build_logical_and(operands[0]);
		for (std::size_t i = 1; i < operands.size(); ++i)
			lhs = build_binary(context, 2 * i - 1, std::move(lhs),
			                   build_logical_and(operands[i]));
		return lhs;
	}

	std::unique_ptr<ast::Expr> build_logical_and(RexyParser::LogicalAndContext *context)
	{
		auto operands = context->comparison();
		auto lhs = build_comparison(operands[0]);
		for (std::size_t i = 1; i < operands.size(); ++i)
			lhs = build_binary(context, 2 * i - 1, std::move(lhs),
			                   build_comparison(operands[i]));
		return lhs;
	}

	std::unique_ptr<ast::Expr> build_comparison(RexyParser::ComparisonContext *context)
	{
		auto operands = context->additive();
		auto lhs = build_additive(operands[0]);
		for (std::size_t i = 1; i < operands.size(); ++i)
			lhs = build_binary(context, 2 * i - 1, std::move(lhs),
			                   build_additive(operands[i]));
		return lhs;
	}

	std::unique_ptr<ast::Expr> build_additive(RexyParser::AdditiveContext *context)
	{
		auto operands = context->multiplicative();
		auto lhs = build_multiplicative(operands[0]);
		for (std::size_t i = 1; i < operands.size(); ++i)
			lhs = build_binary(context, 2 * i - 1, std::move(lhs),
			                   build_multiplicative(operands[i]));
		return lhs;
	}

	std::unique_ptr<ast::Expr> build_multiplicative(
	    RexyParser::MultiplicativeContext *context)
	{
		auto operands = context->cast();
		auto lhs = build_cast(operands[0]);
		for (std::size_t i = 1; i < operands.size(); ++i)
			lhs = build_binary(context, 2 * i - 1, std::move(lhs),
			                   build_cast(operands[i]));
		return lhs;
	}

	std::unique_ptr<ast::Expr> build_cast(RexyParser::CastContext *context)
	{
		auto value = build_unary(context->unary());
		for (auto *type : context->type())
			value = std::make_unique<ast::CastExpr>(
			    location(type), std::move(value), build_type(type));
		return value;
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

	std::unique_ptr<ast::Expr> build_unary(RexyParser::UnaryContext *context)
	{
		if (auto *postfix = context->postfix())
			return build_postfix(postfix);

		if (auto *name = context->IDENT()) {
			std::string op = context->children.front()->getText() == "++" ? "pre++" : "pre--";
			return std::make_unique<ast::UnaryExpr>(
			    location(context), std::move(op),
			    std::make_unique<ast::NameExpr>(location(name), name->getText()));
		}

		auto *operator_node =
		    dynamic_cast<antlr4::tree::TerminalNode *>(context->children.front());
		return std::make_unique<ast::UnaryExpr>(
		    location(operator_node), operator_node != nullptr ? operator_node->getText() : "-",
		    build_unary(context->unary()));
	}

	std::unique_ptr<ast::Expr> build_postfix(RexyParser::PostfixContext *context)
	{
		auto value = build_primary(context->primary());
		for (auto *suffix : context->postfixSuffix()) {
			if (auto *index = suffix->expression()) {
				value = std::make_unique<ast::IndexExpr>(
				    location(index), std::move(value), build_expression(index));
				continue;
			}
			if (auto *field = suffix->IDENT()) {
				value = std::make_unique<ast::FieldAccessExpr>(
				    location(suffix), std::move(value), field->getText());
				continue;
			}
			if (auto *field = suffix->INTEGER()) {
				value = std::make_unique<ast::FieldAccessExpr>(
				    location(suffix), std::move(value), field->getText());
				continue;
			}
			if (suffix->getText() == "?") {
				value = std::make_unique<ast::TryExpr>(
				    location(suffix), std::move(value));
				continue;
			}
		}
		if (!context->children.empty()) {
			std::string suffix = context->children.back()->getText();
			if (suffix == "++" || suffix == "--") {
				value = std::make_unique<ast::UnaryExpr>(
				    location(context), suffix == "++" ? "post++" : "post--",
				    std::move(value));
			}
		}
		return value;
	}

	std::unique_ptr<ast::Expr> build_increment_expression(
	    RexyParser::IncrementExpressionContext *context)
	{
		auto *name = context->IDENT();
		bool prefix = context->children.front()->getText() == "++" ||
		              context->children.front()->getText() == "--";
		std::string token = prefix ? context->children.front()->getText()
		                           : context->children.back()->getText();
		std::string op = std::string(prefix ? "pre" : "post") + token;
		return std::make_unique<ast::UnaryExpr>(
		    location(context), std::move(op),
		    std::make_unique<ast::NameExpr>(location(name), name->getText()));
	}

	std::unique_ptr<ast::Expr> build_primary(RexyParser::PrimaryContext *context)
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

		if (auto *literal = context->structLiteral())
			return build_struct_literal(literal);

		if (auto *tuple = context->tupleExpression())
			return build_tuple_expression(tuple);

		if (auto *call = context->callExpression())
			return build_call_expression(call);

		if (auto *name = context->IDENT())
			return std::make_unique<ast::NameExpr>(location(name), name->getText());

		return build_expression(context->expression());
	}

	std::unique_ptr<ast::Expr> build_tuple_expression(
	    RexyParser::TupleExpressionContext *context)
	{
		auto tuple = std::make_unique<ast::TupleExpr>(location(context));
		for (auto *element : context->expression())
			tuple->elements.push_back(build_expression(element));
		return tuple;
	}

	std::unique_ptr<ast::Expr> build_struct_literal(RexyParser::StructLiteralContext *context)
	{
		auto *name = context->IDENT();
		ast::TypeName type{name->getText(), location(name)};
		auto literal = std::make_unique<ast::StructLiteralExpr>(location(context), type);
		for (auto *field : context->structLiteralField()) {
			auto *field_name = field->IDENT();
			literal->fields.push_back(ast::StructLiteralField{
			    field_name->getText(), build_expression(field->expression()),
			    location(field_name)});
		}
		return literal;
	}

	std::unique_ptr<ast::Expr> build_call_expression(
	    RexyParser::CallExpressionContext *context)
	{
		auto *name = context->qualifiedName()->IDENT().front();
		auto call = std::make_unique<ast::CallExpr>(
		    location(name), build_qualified_name(context->qualifiedName()));
		if (auto *arguments = context->argumentList()) {
			for (auto *argument : arguments->expression())
				call->arguments.push_back(build_expression(argument));
		}
		return call;
	}

	std::vector<std::string> build_qualified_name(RexyParser::QualifiedNameContext *context)
	{
		std::vector<std::string> path;
		for (auto *identifier : context->IDENT())
			path.push_back(identifier->getText());
		return path;
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
	ParseOptions options_;
};

void merge_module(ast::Module &target, ast::Module source)
{
	target.modules.insert(target.modules.end(),
	                      std::make_move_iterator(source.modules.begin()),
	                      std::make_move_iterator(source.modules.end()));
	target.uses.insert(target.uses.end(), std::make_move_iterator(source.uses.begin()),
	                   std::make_move_iterator(source.uses.end()));
	target.static_buffers.insert(target.static_buffers.end(),
	                             std::make_move_iterator(source.static_buffers.begin()),
	                             std::make_move_iterator(source.static_buffers.end()));
	target.static_scalars.insert(target.static_scalars.end(),
	                             std::make_move_iterator(source.static_scalars.begin()),
	                             std::make_move_iterator(source.static_scalars.end()));
	target.structs.insert(target.structs.end(),
	                      std::make_move_iterator(source.structs.begin()),
	                      std::make_move_iterator(source.structs.end()));
	target.enums.insert(target.enums.end(), std::make_move_iterator(source.enums.begin()),
	                    std::make_move_iterator(source.enums.end()));
	target.functions.insert(target.functions.end(),
	                        std::make_move_iterator(source.functions.begin()),
	                        std::make_move_iterator(source.functions.end()));
}

std::string display_path(const std::vector<std::string> &module_path)
{
	std::ostringstream out;
	for (std::size_t i = 0; i < module_path.size(); ++i) {
		if (i > 0)
			out << "::";
		out << module_path[i];
	}
	return out.str();
}

std::string read_text_file(const std::filesystem::path &path)
{
	std::ifstream input(path);
	if (!input)
		return "";
	std::ostringstream buffer;
	buffer << input.rdbuf();
	return buffer.str();
}

class ModuleLoader {
public:
	ModuleLoader(std::filesystem::path entry_path, Diagnostics &diagnostics,
	             ModuleLoadOptions options)
		: entry_path_(std::move(entry_path)), diagnostics_(diagnostics),
		  options_(std::move(options))
	{
		roots_.push_back(std::filesystem::absolute(entry_path_).parent_path());
		for (const auto &package_path : options_.package_paths)
			roots_.push_back(std::filesystem::absolute(package_path).lexically_normal());
	}

	ParseResult run()
	{
		ast::Module module;
		load_file(entry_path_, {}, module);
		return ParseResult(!diagnostics_.has_errors(), std::move(module));
	}

private:
	struct ResolvedModule {
		std::filesystem::path path;
		std::vector<std::filesystem::path> searched;
		bool ambiguous = false;
	};

	void load_file(const std::filesystem::path &path,
	               const std::vector<std::string> &module_path,
	               ast::Module &aggregate)
	{
		auto absolute_path = std::filesystem::absolute(path).lexically_normal();
		if (!parsed_files_.insert(absolute_path.string()).second)
			return;

		auto text = read_text_file(absolute_path);
		if (text.empty() && !std::filesystem::exists(absolute_path)) {
			diagnostics_.error({}, "failed to open input file: " + absolute_path.string());
			return;
		}

		SourceFile source(absolute_path.string(), text);
		auto parsed = parse_source(source, diagnostics_, ParseOptions{module_path});
		if (!parsed.ok())
			return;

		auto parsed_module = parsed.take_module();
		std::vector<ast::ModuleDecl> declarations = parsed_module.modules;
		merge_module(aggregate, std::move(parsed_module));

		for (const auto &declaration : declarations) {
			if (!declaration.is_file_backed)
				continue;
			auto key = display_path(declaration.module_path);
			if (!loaded_modules_.insert(key).second)
				continue;

			auto resolved = resolve_module_file(declaration.module_path);
			if (resolved.ambiguous) {
				diagnostics_.error(declaration.location,
				                   "ambiguous module file '" + key + "'");
				continue;
			}
			if (resolved.path.empty()) {
				diagnostics_.error(declaration.location,
				                   "module file not found '" + key +
				                       "' (searched " + format_searched(resolved.searched) + ")");
				continue;
			}
			load_file(resolved.path, declaration.module_path, aggregate);
		}
	}

	ResolvedModule resolve_module_file(const std::vector<std::string> &module_path) const
	{
		ResolvedModule result;
		for (const auto &root : roots_) {
			auto candidates = candidate_paths(root, module_path);
			result.searched.insert(result.searched.end(), candidates.begin(),
			                       candidates.end());

			std::vector<std::filesystem::path> existing;
			for (const auto &candidate : candidates) {
				if (std::filesystem::exists(candidate))
					existing.push_back(candidate);
			}
			if (existing.size() > 1) {
				result.ambiguous = true;
				return result;
			}
			if (existing.size() == 1) {
				result.path = existing[0];
				return result;
			}
		}
		return result;
	}

	std::vector<std::filesystem::path> candidate_paths(
	    const std::filesystem::path &root,
	    const std::vector<std::string> &module_path) const
	{
		std::filesystem::path base = root;
		for (std::size_t i = 0; i + 1 < module_path.size(); ++i)
			base /= module_path[i];
		std::string leaf = module_path.empty() ? "" : module_path.back();
		return {base / (leaf + ".rx"), base / leaf / "mod.rx"};
	}

	std::string format_searched(const std::vector<std::filesystem::path> &paths) const
	{
		std::ostringstream out;
		for (std::size_t i = 0; i < paths.size(); ++i) {
			if (i > 0)
				out << ", ";
			out << paths[i].string();
		}
		return out.str();
	}

	std::filesystem::path entry_path_;
	Diagnostics &diagnostics_;
	ModuleLoadOptions options_;
	std::vector<std::filesystem::path> roots_;
	std::unordered_set<std::string> parsed_files_;
	std::unordered_set<std::string> loaded_modules_;
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

ParseResult parse_source(const SourceFile &source, Diagnostics &diagnostics,
                         ParseOptions options)
{
	antlr4::ANTLRInputStream input(source.text());
	RexyLexer lexer(&input);
	antlr4::CommonTokenStream tokens(&lexer);
	RexyParser parser(&tokens);
	DiagnosticErrorListener error_listener(source, diagnostics);

	lexer.removeErrorListeners();
	parser.removeErrorListeners();
	lexer.addErrorListener(&error_listener);
	parser.addErrorListener(&error_listener);

	auto *tree = parser.compilationUnit();
	if (diagnostics.has_errors())
		return ParseResult(false, {});

	AstBuilder builder(source, diagnostics, std::move(options));
	auto module = builder.build(tree);
	return ParseResult(!diagnostics.has_errors(), std::move(module));
}

ParseResult parse_file_tree(const std::string &entry_path, Diagnostics &diagnostics,
                            ModuleLoadOptions options)
{
	return ModuleLoader(entry_path, diagnostics, std::move(options)).run();
}

} // namespace rexc
