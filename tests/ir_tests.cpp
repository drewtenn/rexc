#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

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
	REQUIRE(module.functions[0].return_type == rexc::ir::Type::I32);
	REQUIRE_EQ(module.functions[0].body.size(), 1u);
}

TEST_CASE(lowering_lowers_unary_minus_as_zero_minus_operand)
{
	rexc::SourceFile source("test.rx", "fn neg(x: i32) -> i32 { return -x; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());

	const auto &ret = static_cast<const rexc::ir::ReturnStatement &>(*module.functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ir::Value::Kind::Binary);
	const auto &binary = static_cast<const rexc::ir::BinaryValue &>(*ret.value);
	REQUIRE_EQ(binary.op, std::string("-"));
	REQUIRE_EQ(binary.lhs->kind, rexc::ir::Value::Kind::Integer);
	const auto &zero = static_cast<const rexc::ir::IntegerValue &>(*binary.lhs);
	REQUIRE_EQ(zero.value, 0);
	REQUIRE_EQ(binary.rhs->kind, rexc::ir::Value::Kind::Local);
	const auto &local = static_cast<const rexc::ir::LocalValue &>(*binary.rhs);
	REQUIRE_EQ(local.name, std::string("x"));
}
