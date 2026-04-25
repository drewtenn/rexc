// Unit coverage for x86 assembly emission and backend diagnostics.
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

static CodegenAttempt compile_to_codegen_result(
	const std::string &text, rexc::CodegenTarget target = rexc::CodegenTarget::I386)
{
	rexc::SourceFile source("test.rx", text);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());
	auto result = rexc::emit_x86_assembly(rexc::lower_to_ir(parsed.module()), diagnostics, target);
	return {std::move(result), std::move(diagnostics)};
}

static std::string compile_to_assembly(
	const std::string &text, rexc::CodegenTarget target = rexc::CodegenTarget::I386)
{
	auto attempt = compile_to_codegen_result(text, target);
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

TEST_CASE(codegen_i386_emits_comparison_result)
{
	auto assembly = compile_to_assembly("fn main() -> bool { return 1 != 2; }\n");

	REQUIRE(assembly.find("cmpl %ecx, %eax") != std::string::npos);
	REQUIRE(assembly.find("setne %al") != std::string::npos);
	REQUIRE(assembly.find("movzbl %al, %eax") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_if_else_comparison_branch)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { if 1 < 2 { return 7; } else { return 9; } }\n");

	REQUIRE(assembly.find("cmpl %ecx, %eax") != std::string::npos);
	REQUIRE(assembly.find("setl %al") != std::string::npos);
	REQUIRE(assembly.find("cmpb $0, %al") != std::string::npos);
	REQUIRE(assembly.find("je .L_else_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_end_if_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_return_main") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_64_bit_integer_arithmetic)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i64 { let x: i64 = 9223372036854775807; return x + 1; }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("pushq %rbp") != std::string::npos);
	REQUIRE(assembly.find("movq %rsp, %rbp") != std::string::npos);
	REQUIRE(assembly.find("subq $16, %rsp") != std::string::npos);
	REQUIRE(assembly.find("movabsq $9223372036854775807, %rax") != std::string::npos);
	REQUIRE(assembly.find("movq %rax, -8(%rbp)") != std::string::npos);
	REQUIRE(assembly.find("addq %rcx, %rax") != std::string::npos);
	REQUIRE(assembly.find("ret") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_if_else_comparison_branch)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i64 { if 1 <= 2 { return 7; } else { return 9; } }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("cmpq %rcx, %rax") != std::string::npos);
	REQUIRE(assembly.find("setle %al") != std::string::npos);
	REQUIRE(assembly.find("movzbq %al, %rax") != std::string::npos);
	REQUIRE(assembly.find("cmpb $0, %al") != std::string::npos);
	REQUIRE(assembly.find("je .L_else_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_end_if_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_return_main") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_assignment_and_while_loop)
{
	auto assembly = compile_to_assembly(
	    "fn main() -> i32 { let mut x: i32 = 0; while x < 3 { x = x + 1; } return x; }\n");

	REQUIRE(assembly.find(".L_while_start_") != std::string::npos);
	REQUIRE(assembly.find(".L_while_end_") != std::string::npos);
	REQUIRE(assembly.find("je .L_while_end_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_while_start_") != std::string::npos);
	REQUIRE(assembly.find("movl %eax, -4(%ebp)") != std::string::npos);
	REQUIRE_EQ(count_occurrences(assembly, "subl $4, %esp"), 1);
}

TEST_CASE(codegen_x86_64_emits_assignment_and_while_loop)
{
	auto assembly = compile_to_assembly(
	    "fn main() -> i64 { let mut x: i64 = 0; while x < 3 { x = x + 1; } return x; }\n",
	    rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find(".L_while_start_") != std::string::npos);
	REQUIRE(assembly.find(".L_while_end_") != std::string::npos);
	REQUIRE(assembly.find("je .L_while_end_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_while_start_") != std::string::npos);
	REQUIRE(assembly.find("movq %rax, -8(%rbp)") != std::string::npos);
	REQUIRE_EQ(count_occurrences(assembly, "subq $16, %rsp"), 1);
}

TEST_CASE(codegen_x86_64_uses_system_v_argument_registers)
{
	auto assembly = compile_to_assembly(
		"fn add(a: i64, b: i64) -> i64 { return a + b; }\n"
		"fn main() -> i64 { return add(20, 22); }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("movq %rdi, -8(%rbp)") != std::string::npos);
	REQUIRE(assembly.find("movq %rsi, -16(%rbp)") != std::string::npos);
	REQUIRE(assembly.find("popq %rdi") != std::string::npos);
	REQUIRE(assembly.find("popq %rsi") != std::string::npos);
	REQUIRE(assembly.find("call add") != std::string::npos);
	REQUIRE(assembly.find("addq $") == std::string::npos);
}

TEST_CASE(codegen_x86_64_passes_seventh_argument_on_stack)
{
	auto assembly = compile_to_assembly(
		"fn pick(a: i64, b: i64, c: i64, d: i64, e: i64, f: i64, g: i64) -> i64 { return g; }\n"
		"fn main() -> i64 { return pick(1, 2, 3, 4, 5, 6, 7); }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("movq 16(%rbp), %rax") != std::string::npos);
	REQUIRE(assembly.find("subq $8, %rsp") != std::string::npos);
	REQUIRE(assembly.find("addq $16, %rsp") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_strings_and_unsigned_division)
{
	auto assembly = compile_to_assembly(
		"fn main() -> u64 { let s: str = \"wide\"; return 18446744073709551615 / 2; }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find(".section .rodata") != std::string::npos);
	REQUIRE(assembly.find(".asciz \"wide\"") != std::string::npos);
	REQUIRE(assembly.find("movq $.Lstr0, %rax") != std::string::npos);
	REQUIRE(assembly.find("movabsq $18446744073709551615, %rax") != std::string::npos);
	REQUIRE(assembly.find("xorq %rdx, %rdx") != std::string::npos);
	REQUIRE(assembly.find("divq %rcx") != std::string::npos);
}
