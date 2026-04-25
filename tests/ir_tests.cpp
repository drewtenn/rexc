#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <stdexcept>
#include <string>

namespace {

void require_lowering_throws_for(const std::string &text)
{
	rexc::SourceFile source("test.rx", text);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	try {
		(void)rexc::lower_to_ir(parsed.module());
		REQUIRE(false);
	} catch (const std::runtime_error &err) {
		REQUIRE(std::string(err.what()).find("integer literal is not supported by current IR lowering") !=
		        std::string::npos);
	}
}

void require_lowering_throws_for(const std::string &text, const std::string &message)
{
	rexc::SourceFile source("test.rx", text);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	try {
		(void)rexc::lower_to_ir(parsed.module());
		REQUIRE(false);
	} catch (const std::runtime_error &err) {
		REQUIRE(std::string(err.what()).find(message) != std::string::npos);
	}
}

} // namespace

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

TEST_CASE(lowering_allows_i32_min_integer_literal)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { let x: i32 = -2147483648; return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	(void)rexc::lower_to_ir(parsed.module());
}

TEST_CASE(lowering_rejects_integer_literals_that_current_ir_cannot_represent)
{
	require_lowering_throws_for("fn main() -> i32 { let x: u64 = 18446744073709551615; return 0; }\n");
	require_lowering_throws_for("fn main() -> i32 { let x: i64 = -9223372036854775808; return 0; }\n");
}

TEST_CASE(lowering_rejects_types_that_current_ir_cannot_represent)
{
	require_lowering_throws_for("fn id(x: i64) -> i64 { return x; }\n",
	                            "type is not supported by current IR lowering: i64");
	require_lowering_throws_for("fn main() -> u64 { return 1; }\n",
	                            "type is not supported by current IR lowering: u64");
	require_lowering_throws_for("fn main() -> i32 { let b: bool = true; return 0; }\n",
	                            "literal type is not supported by current IR lowering: bool");
}
