// Hand-written lexer and recursive-descent parser for the Rexc grammar.
#include "rexc/parse.hpp"

#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rexc {
namespace {

enum class TokenKind {
	End,
	Identifier,
	Integer,
	Bool,
	Char,
	String,
	Extern,
	Fn,
	Let,
	Return,
	LParen,
	RParen,
	LBrace,
	RBrace,
	Colon,
	Semicolon,
	Comma,
	Arrow,
	Equal,
	Plus,
	Minus,
	Star,
	Slash,
};

struct Token {
	TokenKind kind = TokenKind::End;
	std::string text;
	std::size_t offset = 0;
};

class Lexer {
public:
	explicit Lexer(const SourceFile &source) : source_(source) {}

	std::vector<Token> lex(Diagnostics &diagnostics)
	{
		std::vector<Token> tokens;

		while (offset_ < source_.text().size()) {
			char ch = source_.text()[offset_];
			if (std::isspace(static_cast<unsigned char>(ch))) {
				++offset_;
				continue;
			}

			if (ch == '/' && peek(1) == '/') {
				while (offset_ < source_.text().size() && source_.text()[offset_] != '\n')
					++offset_;
				continue;
			}

			if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
				tokens.push_back(identifier());
				continue;
			}

			if (std::isdigit(static_cast<unsigned char>(ch))) {
				tokens.push_back(integer());
				continue;
			}

			if (ch == '\'') {
				tokens.push_back(character(diagnostics));
				continue;
			}

			if (ch == '"') {
				tokens.push_back(string(diagnostics));
				continue;
			}

			std::size_t start = offset_++;
			switch (ch) {
			case '(':
				tokens.push_back({TokenKind::LParen, "(", start});
				break;
			case ')':
				tokens.push_back({TokenKind::RParen, ")", start});
				break;
			case '{':
				tokens.push_back({TokenKind::LBrace, "{", start});
				break;
			case '}':
				tokens.push_back({TokenKind::RBrace, "}", start});
				break;
			case ':':
				tokens.push_back({TokenKind::Colon, ":", start});
				break;
			case ';':
				tokens.push_back({TokenKind::Semicolon, ";", start});
				break;
			case ',':
				tokens.push_back({TokenKind::Comma, ",", start});
				break;
			case '=':
				tokens.push_back({TokenKind::Equal, "=", start});
				break;
			case '+':
				tokens.push_back({TokenKind::Plus, "+", start});
				break;
			case '*':
				tokens.push_back({TokenKind::Star, "*", start});
				break;
			case '/':
				tokens.push_back({TokenKind::Slash, "/", start});
				break;
			case '-':
				if (peek(0) == '>') {
					++offset_;
					tokens.push_back({TokenKind::Arrow, "->", start});
				} else {
					tokens.push_back({TokenKind::Minus, "-", start});
				}
				break;
			default:
				diagnostics.error(source_.location_at(start),
				                  std::string("unexpected character '") + ch + "'");
				break;
			}
		}

		tokens.push_back({TokenKind::End, "", source_.text().size()});
		return tokens;
	}

private:
	char peek(std::size_t ahead) const
	{
		std::size_t at = offset_ + ahead;
		return at < source_.text().size() ? source_.text()[at] : '\0';
	}

	Token identifier()
	{
		std::size_t start = offset_;
		while (offset_ < source_.text().size()) {
			char ch = source_.text()[offset_];
			if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_')
				break;
			++offset_;
		}

		std::string text = source_.text().substr(start, offset_ - start);
		if (text == "extern")
			return {TokenKind::Extern, text, start};
		if (text == "fn")
			return {TokenKind::Fn, text, start};
		if (text == "let")
			return {TokenKind::Let, text, start};
		if (text == "return")
			return {TokenKind::Return, text, start};
		if (text == "true" || text == "false")
			return {TokenKind::Bool, text, start};
		return {TokenKind::Identifier, text, start};
	}

	Token integer()
	{
		std::size_t start = offset_;
		while (offset_ < source_.text().size() &&
		       std::isdigit(static_cast<unsigned char>(source_.text()[offset_])))
			++offset_;
		return {TokenKind::Integer, source_.text().substr(start, offset_ - start), start};
	}

	char decode_escape(Diagnostics &diagnostics, std::size_t escape_offset)
	{
		if (offset_ >= source_.text().size()) {
			diagnostics.error(source_.location_at(escape_offset), "unterminated escape sequence");
			return '\0';
		}

		char escaped = source_.text()[offset_++];
		switch (escaped) {
		case 'n':
			return '\n';
		case 'r':
			return '\r';
		case 't':
			return '\t';
		case '\'':
			return '\'';
		case '"':
			return '"';
		case '\\':
			return '\\';
		default:
			diagnostics.error(source_.location_at(escape_offset),
			                  std::string("unknown escape sequence '\\") + escaped + "'");
			return escaped;
		}
	}

	Token character(Diagnostics &diagnostics)
	{
		std::size_t start = offset_++;
		std::string value;

		if (offset_ >= source_.text().size()) {
			diagnostics.error(source_.location_at(start), "unterminated character literal");
			return {TokenKind::Char, value, start};
		}

		if (source_.text()[offset_] == '\\') {
			std::size_t escape_offset = offset_++;
			value.push_back(decode_escape(diagnostics, escape_offset));
		} else if (source_.text()[offset_] == '\'' || source_.text()[offset_] == '\n' ||
		           source_.text()[offset_] == '\r') {
			diagnostics.error(source_.location_at(offset_), "expected character literal value");
		} else {
			value.push_back(source_.text()[offset_++]);
		}

		if (offset_ < source_.text().size() && source_.text()[offset_] == '\'') {
			++offset_;
		} else {
			diagnostics.error(source_.location_at(offset_), "unterminated character literal");
			while (offset_ < source_.text().size() && source_.text()[offset_] != '\'' &&
			       source_.text()[offset_] != '\n' && source_.text()[offset_] != '\r')
				++offset_;
			if (offset_ < source_.text().size() && source_.text()[offset_] == '\'')
				++offset_;
		}

		return {TokenKind::Char, value, start};
	}

	Token string(Diagnostics &diagnostics)
	{
		std::size_t start = offset_++;
		std::string value;

		while (offset_ < source_.text().size()) {
			char ch = source_.text()[offset_++];
			if (ch == '"')
				return {TokenKind::String, value, start};
			if (ch == '\n' || ch == '\r') {
				diagnostics.error(source_.location_at(offset_ - 1), "unterminated string literal");
				return {TokenKind::String, value, start};
			}
			if (ch == '\\') {
				value.push_back(decode_escape(diagnostics, offset_ - 1));
				continue;
			}
			value.push_back(ch);
		}

		diagnostics.error(source_.location_at(start), "unterminated string literal");
		return {TokenKind::String, value, start};
	}

	const SourceFile &source_;
	std::size_t offset_ = 0;
};

class Parser {
public:
	Parser(const SourceFile &source, Diagnostics &diagnostics, std::vector<Token> tokens)
		: source_(source), diagnostics_(diagnostics), tokens_(std::move(tokens))
	{
	}

	ParseResult parse()
	{
		ast::Module module;

		while (!at(TokenKind::End)) {
			if (at(TokenKind::Extern))
				module.functions.push_back(parse_extern_function());
			else if (at(TokenKind::Fn))
				module.functions.push_back(parse_function_definition());
			else {
				error_here("expected item");
				advance();
			}
		}

		return ParseResult(!diagnostics_.has_errors(), std::move(module));
	}

private:
	bool at(TokenKind kind) const
	{
		return current().kind == kind;
	}

	const Token &current() const
	{
		return tokens_[index_];
	}

	const Token &next() const
	{
		return tokens_[index_ + 1 < tokens_.size() ? index_ + 1 : index_];
	}

	const Token &advance()
	{
		const Token &token = current();
		if (!at(TokenKind::End))
			++index_;
		return token;
	}

	Token expect(TokenKind kind, const char *message)
	{
		if (at(kind))
			return advance();
		error_here(message);
		return {kind, "", current().offset};
	}

	void error_here(const std::string &message)
	{
		diagnostics_.error(source_.location_at(current().offset), message);
	}

	ast::Function parse_extern_function()
	{
		Token start = expect(TokenKind::Extern, "expected 'extern'");
		expect(TokenKind::Fn, "expected 'fn'");
		ast::Function function = parse_signature(start.offset);
		function.is_extern = true;
		expect(TokenKind::Semicolon, "expected ';'");
		return function;
	}

	ast::Function parse_function_definition()
	{
		Token start = expect(TokenKind::Fn, "expected 'fn'");
		ast::Function function = parse_signature(start.offset);
		function.body = parse_block();
		return function;
	}

	ast::Function parse_signature(std::size_t start_offset)
	{
		ast::Function function;
		function.location = source_.location_at(start_offset);
		Token name = expect(TokenKind::Identifier, "expected function name");
		function.name = name.text;
		expect(TokenKind::LParen, "expected '('");
		if (!at(TokenKind::RParen))
			function.parameters = parse_parameter_list();
		expect(TokenKind::RParen, "expected ')'");
		expect(TokenKind::Arrow, "expected '->'");
		function.return_type = parse_type();
		return function;
	}

	std::vector<ast::Parameter> parse_parameter_list()
	{
		std::vector<ast::Parameter> parameters;
		parameters.push_back(parse_parameter());
		while (at(TokenKind::Comma)) {
			advance();
			parameters.push_back(parse_parameter());
		}
		return parameters;
	}

	ast::Parameter parse_parameter()
	{
		Token name = expect(TokenKind::Identifier, "expected parameter name");
		expect(TokenKind::Colon, "expected ':'");
		return ast::Parameter{name.text, parse_type(), source_.location_at(name.offset)};
	}

	ast::TypeName parse_type()
	{
		if (!at(TokenKind::Identifier)) {
			Token token = expect(TokenKind::Identifier, "expected type");
			return ast::TypeName{token.text, source_.location_at(token.offset)};
		}

		Token token = advance();
		if (!is_primitive_type_name(token.text))
			diagnostics_.error(source_.location_at(token.offset), "expected primitive type");
		return ast::TypeName{token.text, source_.location_at(token.offset)};
	}

	std::vector<std::unique_ptr<ast::Stmt>> parse_block()
	{
		std::vector<std::unique_ptr<ast::Stmt>> body;
		expect(TokenKind::LBrace, "expected '{'");
		while (!at(TokenKind::RBrace) && !at(TokenKind::End)) {
			if (at(TokenKind::Let))
				body.push_back(parse_let_statement());
			else if (at(TokenKind::Return))
				body.push_back(parse_return_statement());
			else {
				error_here("expected statement");
				advance();
			}
		}
		expect(TokenKind::RBrace, "expected '}'");
		return body;
	}

	std::unique_ptr<ast::Stmt> parse_let_statement()
	{
		Token start = expect(TokenKind::Let, "expected 'let'");
		Token name = expect(TokenKind::Identifier, "expected local name");
		expect(TokenKind::Colon, "expected ':'");
		auto type = parse_type();
		expect(TokenKind::Equal, "expected '='");
		auto initializer = parse_expression();
		expect(TokenKind::Semicolon, "expected ';'");
		return std::make_unique<ast::LetStmt>(source_.location_at(start.offset), name.text,
		                                      std::move(type), std::move(initializer));
	}

	std::unique_ptr<ast::Stmt> parse_return_statement()
	{
		Token start = expect(TokenKind::Return, "expected 'return'");
		auto value = parse_expression();
		expect(TokenKind::Semicolon, "expected ';'");
		return std::make_unique<ast::ReturnStmt>(source_.location_at(start.offset), std::move(value));
	}

	std::unique_ptr<ast::Expr> parse_expression()
	{
		return parse_additive();
	}

	bool is_primitive_type_name(const std::string &name) const
	{
		return name == "i8" || name == "i16" || name == "i32" || name == "i64" ||
		       name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
		       name == "bool" || name == "char" || name == "str";
	}

	std::unique_ptr<ast::Expr> parse_additive()
	{
		auto lhs = parse_multiplicative();
		while (at(TokenKind::Plus) || at(TokenKind::Minus)) {
			Token op = advance();
			auto rhs = parse_multiplicative();
			lhs = std::make_unique<ast::BinaryExpr>(source_.location_at(op.offset), op.text,
			                                        std::move(lhs), std::move(rhs));
		}
		return lhs;
	}

	std::unique_ptr<ast::Expr> parse_multiplicative()
	{
		auto lhs = parse_unary();
		while (at(TokenKind::Star) || at(TokenKind::Slash)) {
			Token op = advance();
			auto rhs = parse_unary();
			lhs = std::make_unique<ast::BinaryExpr>(source_.location_at(op.offset), op.text,
			                                        std::move(lhs), std::move(rhs));
		}
		return lhs;
	}

	std::unique_ptr<ast::Expr> parse_unary()
	{
		if (at(TokenKind::Minus)) {
			Token op = advance();
			return std::make_unique<ast::UnaryExpr>(source_.location_at(op.offset), op.text,
			                                       parse_unary());
		}

		return parse_primary();
	}

	std::unique_ptr<ast::Expr> parse_primary()
	{
		if (at(TokenKind::Integer)) {
			Token token = advance();
			return std::make_unique<ast::IntegerExpr>(source_.location_at(token.offset),
			                                          std::strtoll(token.text.c_str(), nullptr, 10),
			                                          token.text);
		}

		if (at(TokenKind::Bool)) {
			Token token = advance();
			return std::make_unique<ast::BoolExpr>(source_.location_at(token.offset),
			                                       token.text == "true");
		}

		if (at(TokenKind::Char)) {
			Token token = advance();
			char32_t value = token.text.empty()
			                     ? U'\0'
			                     : static_cast<unsigned char>(token.text.front());
			return std::make_unique<ast::CharExpr>(source_.location_at(token.offset), value);
		}

		if (at(TokenKind::String)) {
			Token token = advance();
			return std::make_unique<ast::StringExpr>(source_.location_at(token.offset), token.text);
		}

		if (at(TokenKind::Identifier)) {
			Token token = advance();
			if (at(TokenKind::LParen))
				return finish_call(token);
			return std::make_unique<ast::NameExpr>(source_.location_at(token.offset), token.text);
		}

		if (at(TokenKind::LParen)) {
			advance();
			auto expr = parse_expression();
			expect(TokenKind::RParen, "expected ')'");
			return expr;
		}

		error_here("expected expression");
		Token token = advance();
		return std::make_unique<ast::IntegerExpr>(source_.location_at(token.offset), 0, "0");
	}

	std::unique_ptr<ast::Expr> finish_call(const Token &callee)
	{
		auto call = std::make_unique<ast::CallExpr>(source_.location_at(callee.offset), callee.text);
		expect(TokenKind::LParen, "expected '('");
		if (!at(TokenKind::RParen)) {
			call->arguments.push_back(parse_expression());
			while (at(TokenKind::Comma)) {
				advance();
				call->arguments.push_back(parse_expression());
			}
		}
		expect(TokenKind::RParen, "expected ')'");
		return call;
	}

	const SourceFile &source_;
	Diagnostics &diagnostics_;
	std::vector<Token> tokens_;
	std::size_t index_ = 0;
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
	Lexer lexer(source);
	auto tokens = lexer.lex(diagnostics);
	Parser parser(source, diagnostics, std::move(tokens));
	return parser.parse();
}

} // namespace rexc
