#include "rexc/codegen_x86.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <stdexcept>
#include <string>

static std::string compile_to_assembly(const std::string &text)
{
	rexc::SourceFile source("test.rx", text);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());
	return rexc::emit_x86_assembly(rexc::lower_to_ir(parsed.module()));
}

TEST_CASE(codegen_emits_main_returning_integer)
{
	auto assembly = compile_to_assembly("fn main() -> i32 { return 42; }\n");

	REQUIRE(assembly.find(".globl main") != std::string::npos);
	REQUIRE(assembly.find("main:") != std::string::npos);
	REQUIRE(assembly.find("movl $42, %eax") != std::string::npos);
	REQUIRE(assembly.find("leave") != std::string::npos);
	REQUIRE(assembly.find("ret") != std::string::npos);
}

TEST_CASE(codegen_emits_call_and_caller_stack_cleanup)
{
	auto assembly = compile_to_assembly(
		"fn add(a: i32, b: i32) -> i32 { return a + b; }\n"
		"fn main() -> i32 { let value: i32 = add(20, 22); return value; }\n");

	REQUIRE(assembly.find("call add") != std::string::npos);
	REQUIRE(assembly.find("addl $8, %esp") != std::string::npos);
	REQUIRE(assembly.find("-4(%ebp)") != std::string::npos);
}

TEST_CASE(codegen_emits_decimal_integer_immediates_without_octal_spelling)
{
	auto assembly = compile_to_assembly("fn main() -> i32 { return 010; }\n");

	REQUIRE(assembly.find("movl $10, %eax") != std::string::npos);
	REQUIRE(assembly.find("movl $010, %eax") == std::string::npos);
}

TEST_CASE(codegen_rejects_unsupported_64_bit_integer_literals)
{
	try {
		(void)compile_to_assembly("fn main() -> i32 { let x: u64 = 18446744073709551615; return 0; }\n");
		REQUIRE(false);
	} catch (const std::runtime_error &err) {
		REQUIRE(std::string(err.what()).find("64-bit integer code generation is not implemented") !=
		        std::string::npos);
	}
}

TEST_CASE(codegen_rejects_string_literals_until_rodata_codegen_exists)
{
	try {
		(void)compile_to_assembly("fn main() -> i32 { let s: str = \"hi\"; return 0; }\n");
		REQUIRE(false);
	} catch (const std::runtime_error &err) {
		REQUIRE(std::string(err.what()).find("string code generation is not implemented") !=
		        std::string::npos);
	}
}
