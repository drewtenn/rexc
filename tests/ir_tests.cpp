// IR lowering tests that check typed values survive frontend stages.
#include "rexc/ast.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <memory>
#include <stdexcept>
#include <string>

TEST_CASE(lowering_preserves_function_signature)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 42; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions.size(), 1u);
	REQUIRE_EQ(module.functions[0].name, std::string("main"));
	REQUIRE_EQ(rexc::format_type(module.functions[0].return_type), std::string("i32"));
	REQUIRE_EQ(module.functions[0].body.size(), 1u);
}

TEST_CASE(lowering_lowers_unary_minus_as_typed_unary_value)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return -1; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Unary);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("i32"));
	const auto &unary = static_cast<const rexc::ir::UnaryValue &>(*ret.value);
	REQUIRE_EQ(unary.op, std::string("-"));
	REQUIRE_EQ(unary.operand->kind, rexc::ir::Value::Kind::Integer);
	const auto &integer = static_cast<const rexc::ir::IntegerValue &>(*unary.operand);
	REQUIRE_EQ(integer.literal, std::string("1"));
	REQUIRE(!integer.is_negative);
	REQUIRE_EQ(rexc::format_type(integer.type), std::string("i32"));
}

TEST_CASE(lowering_allows_i32_min_integer_literal)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { let x: i32 = -2147483648; return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	(void)rexc::lower_to_ir(parsed.module());
}

TEST_CASE(lowering_preserves_non_i32_types_and_literals)
{
	rexc::SourceFile source(
		"test.rx",
		"fn main() -> u32 { let ok: bool = true; let c: char = 'x'; let s: str = \"hi\"; let n: u32 = 42; return n; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions.size(), 1u);
	const auto &function = module.functions[0];
	REQUIRE_EQ(rexc::format_type(function.return_type), std::string("u32"));
	REQUIRE_EQ(function.body.size(), 5u);

	const auto &ok = static_cast<const rexc::ir::LetStatement &>(*function.body[0]);
	REQUIRE_EQ(ok.value->kind, rexc::ir::Value::Kind::Bool);
	REQUIRE_EQ(rexc::format_type(ok.value->type), std::string("bool"));
	const auto &ok_value = static_cast<const rexc::ir::BoolValue &>(*ok.value);
	REQUIRE(ok_value.value);

	const auto &c = static_cast<const rexc::ir::LetStatement &>(*function.body[1]);
	REQUIRE_EQ(c.value->kind, rexc::ir::Value::Kind::Char);
	REQUIRE_EQ(rexc::format_type(c.value->type), std::string("char"));
	const auto &char_value = static_cast<const rexc::ir::CharValue &>(*c.value);
	REQUIRE_EQ(char_value.value, U'x');

	const auto &s = static_cast<const rexc::ir::LetStatement &>(*function.body[2]);
	REQUIRE_EQ(s.value->kind, rexc::ir::Value::Kind::String);
	REQUIRE_EQ(rexc::format_type(s.value->type), std::string("str"));
	const auto &string_value = static_cast<const rexc::ir::StringValue &>(*s.value);
	REQUIRE_EQ(string_value.value, std::string("hi"));

	const auto &n = static_cast<const rexc::ir::LetStatement &>(*function.body[3]);
	REQUIRE_EQ(n.value->kind, rexc::ir::Value::Kind::Integer);
	REQUIRE_EQ(rexc::format_type(n.value->type), std::string("u32"));
	const auto &integer_value = static_cast<const rexc::ir::IntegerValue &>(*n.value);
	REQUIRE_EQ(integer_value.literal, std::string("42"));
	REQUIRE(!integer_value.is_negative);

	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*function.body[4]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Local);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("u32"));
}

TEST_CASE(lowering_preserves_non_i32_parameter_types)
{
	rexc::SourceFile source("test.rx", "fn id(x: u64, ok: bool) -> u64 { return x; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	REQUIRE_EQ(module.functions.size(), 1u);
	const auto &function = module.functions[0];
	REQUIRE_EQ(rexc::format_type(function.return_type), std::string("u64"));
	REQUIRE_EQ(function.parameters.size(), 2u);
	REQUIRE_EQ(function.parameters[0].name, std::string("x"));
	REQUIRE_EQ(rexc::format_type(function.parameters[0].type), std::string("u64"));
	REQUIRE_EQ(function.parameters[1].name, std::string("ok"));
	REQUIRE_EQ(rexc::format_type(function.parameters[1].type), std::string("bool"));

	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*function.body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Local);
	REQUIRE_EQ(rexc::format_type(ret.value->type), std::string("u64"));
}

TEST_CASE(lowering_preserves_integer_literal_text_that_exceeds_i32_storage)
{
	rexc::SourceFile source(
		"test.rx",
		"fn main() -> i32 { let max: u64 = 18446744073709551615; let min: i64 = -9223372036854775808; return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	const auto &max = static_cast<const rexc::ir::LetStatement &>(*module.functions[0].body[0]);
	const auto &max_value = static_cast<const rexc::ir::IntegerValue &>(*max.value);
	REQUIRE_EQ(rexc::format_type(max_value.type), std::string("u64"));
	REQUIRE_EQ(max_value.literal, std::string("18446744073709551615"));
	REQUIRE(!max_value.is_negative);

	const auto &min = static_cast<const rexc::ir::LetStatement &>(*module.functions[0].body[1]);
	REQUIRE_EQ(min.value->kind, rexc::ir::Value::Kind::Unary);
	REQUIRE_EQ(rexc::format_type(min.value->type), std::string("i64"));
	const auto &min_value = static_cast<const rexc::ir::UnaryValue &>(*min.value);
	REQUIRE_EQ(min_value.op, std::string("-"));
	REQUIRE_EQ(min_value.operand->kind, rexc::ir::Value::Kind::Integer);
	const auto &min_integer = static_cast<const rexc::ir::IntegerValue &>(*min_value.operand);
	REQUIRE_EQ(rexc::format_type(min_integer.type), std::string("i64"));
	REQUIRE_EQ(min_integer.literal, std::string("9223372036854775808"));
	REQUIRE(!min_integer.is_negative);
}

TEST_CASE(lowering_rejects_invalid_type_names)
{
	rexc::SourceLocation location{"test.rx", 0, 1, 1};
	rexc::ast::Module module;
	rexc::ast::Function function;
	function.name = "main";
	function.return_type = {"mystery", location};
	function.location = location;
	function.body.push_back(std::make_unique<rexc::ast::ReturnStmt>(
		location, std::make_unique<rexc::ast::IntegerExpr>(location, 1, "1")));
	module.functions.push_back(std::move(function));

	try {
		(void)rexc::lower_to_ir(module);
		REQUIRE(false);
	} catch (const std::runtime_error &err) {
		REQUIRE(std::string(err.what()).find("unknown primitive type in IR lowering: mystery") !=
		        std::string::npos);
	}
}
