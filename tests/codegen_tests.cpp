// Unit coverage for i386 assembly emission and backend diagnostics.
#include "rexc/codegen_x86.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <string>
#include <utility>

struct CodegenAttempt {
	rexc::CodegenResult result;
	rexc::Diagnostics diagnostics;
};

static CodegenAttempt compile_to_codegen_result(const std::string &text)
{
	rexc::SourceFile source("test.rx", text);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());
	auto result = rexc::emit_x86_assembly(rexc::lower_to_ir(parsed.module()), diagnostics);
	return {std::move(result), std::move(diagnostics)};
}

static std::string compile_to_assembly(const std::string &text)
{
	auto attempt = compile_to_codegen_result(text);
	REQUIRE(attempt.result.ok());
	REQUIRE(!attempt.diagnostics.has_errors());
	return attempt.result.assembly();
}

static int count_occurrences(const std::string &text, const std::string &needle)
{
	int count = 0;
	std::size_t offset = 0;
	while ((offset = text.find(needle, offset)) != std::string::npos) {
		++count;
		offset += needle.size();
	}
	return count;
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

TEST_CASE(codegen_emits_bool_char_and_string_literals)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 {\n"
		"  let ok: bool = true;\n"
		"  let c: char = 'A';\n"
		"  let s: str = \"hi\";\n"
		"  return 0;\n"
		"}\n");

	auto rodata = assembly.find(".section .rodata");
	auto text = assembly.find(".text");
	REQUIRE(rodata != std::string::npos);
	REQUIRE(text != std::string::npos);
	REQUIRE(rodata < text);
	REQUIRE(assembly.find(".Lstr0:") != std::string::npos);
	REQUIRE(assembly.find(".asciz \"hi\"") != std::string::npos);
	REQUIRE(assembly.find("movl $1, %eax") != std::string::npos);
	REQUIRE(assembly.find("movl $65, %eax") != std::string::npos);
	REQUIRE(assembly.find("movl $.Lstr0, %eax") != std::string::npos);
}

TEST_CASE(codegen_escapes_string_literals_for_asciz)
{
	auto assembly = compile_to_assembly(
		R"(fn main() -> i32 {
  let s: str = "line\n\"slash\\tab\t";
  return 0;
}
)");

	REQUIRE(assembly.find(R"(.asciz "line\n\"slash\\tab\t")") != std::string::npos);
}

TEST_CASE(codegen_uses_32_bit_stack_slots_for_mixed_primitive_locals)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 {\n"
		"  let a: i8 = 1;\n"
		"  let b: u16 = 2;\n"
		"  let c: i32 = 3;\n"
		"  let ok: bool = true;\n"
		"  let ch: char = 'A';\n"
		"  let s: str = \"hi\";\n"
		"  return c;\n"
		"}\n");

	REQUIRE(assembly.find("subl $24, %esp") != std::string::npos);
	REQUIRE(assembly.find("-4(%ebp)") != std::string::npos);
	REQUIRE(assembly.find("-8(%ebp)") != std::string::npos);
	REQUIRE(assembly.find("-12(%ebp)") != std::string::npos);
	REQUIRE(assembly.find("-16(%ebp)") != std::string::npos);
	REQUIRE(assembly.find("-20(%ebp)") != std::string::npos);
	REQUIRE(assembly.find("-24(%ebp)") != std::string::npos);
}

TEST_CASE(codegen_reports_unsupported_64_bit_integer_literals_as_diagnostics)
{
	auto attempt = compile_to_codegen_result(
		"fn main() -> i32 { let x: u64 = 18446744073709551615; return 0; }\n");

	REQUIRE(!attempt.result.ok());
	REQUIRE(attempt.diagnostics.has_errors());
	REQUIRE(attempt.diagnostics.format().find("64-bit integer code generation is not implemented for i386") !=
	        std::string::npos);
}

TEST_CASE(codegen_reports_unsupported_64_bit_integer_signatures_as_diagnostics)
{
	auto attempt = compile_to_codegen_result("fn f(x: i64, y: i32) -> i32 { return y; }\n");

	REQUIRE(!attempt.result.ok());
	REQUIRE(attempt.diagnostics.has_errors());
	REQUIRE(attempt.diagnostics.format().find("64-bit integer code generation is not implemented for i386") !=
	        std::string::npos);
}

TEST_CASE(codegen_reports_unsupported_64_bit_extern_signatures_as_diagnostics)
{
	auto attempt = compile_to_codegen_result(
		"extern fn f(x: i64) -> u64;\n"
		"fn main() -> i32 { return 0; }\n");

	REQUIRE(!attempt.result.ok());
	REQUIRE(attempt.diagnostics.has_errors());
	REQUIRE(attempt.diagnostics.format().find("64-bit integer code generation is not implemented for i386") !=
	        std::string::npos);
}

TEST_CASE(codegen_returns_empty_assembly_when_module_validation_fails)
{
	auto attempt = compile_to_codegen_result(
		"fn bad() -> i64 { return 1; }\n"
		"fn main() -> i32 { return 0; }\n");

	REQUIRE(!attempt.result.ok());
	REQUIRE(attempt.result.assembly().empty());
	REQUIRE(attempt.result.assembly().find(".globl main") == std::string::npos);
}

TEST_CASE(codegen_deduplicates_unsupported_64_bit_diagnostics)
{
	auto attempt = compile_to_codegen_result(
		"fn main() -> i64 { let x: i64 = 1; return x; }\n");
	const std::string message = "64-bit integer code generation is not implemented for i386";

	REQUIRE(!attempt.result.ok());
	REQUIRE_EQ(count_occurrences(attempt.diagnostics.format(), message), 1);
}

TEST_CASE(codegen_reports_unsupported_64_bit_diagnostics_per_function)
{
	auto attempt = compile_to_codegen_result(
		"fn a() -> i64 { return 1; }\n"
		"fn b() -> u64 { return 2; }\n");
	const std::string message = "64-bit integer code generation is not implemented for i386";

	REQUIRE(!attempt.result.ok());
	REQUIRE_EQ(count_occurrences(attempt.diagnostics.format(), message), 2);
}

TEST_CASE(codegen_emits_signed_division)
{
	auto assembly = compile_to_assembly("fn main() -> i32 { return 7 / 2; }\n");

	REQUIRE(assembly.find("cltd") != std::string::npos);
	REQUIRE(assembly.find("idivl %ecx") != std::string::npos);
	REQUIRE(assembly.find("\tdivl %ecx") == std::string::npos);
}

TEST_CASE(codegen_emits_unsigned_division)
{
	auto assembly = compile_to_assembly("fn main() -> u32 { return 4000000000 / 2; }\n");

	REQUIRE(assembly.find("xorl %edx, %edx") != std::string::npos);
	REQUIRE(assembly.find("divl %ecx") != std::string::npos);
	REQUIRE(assembly.find("cltd") == std::string::npos);
	REQUIRE(assembly.find("idivl %ecx") == std::string::npos);
}
