// Unit coverage for x86 assembly emission and backend diagnostics.
//
// These tests parse real Rexc snippets, run sema and IR lowering, then inspect
// emitted assembly. That keeps backend checks tied to the same frontend shapes
// users write instead of constructing IR by hand for every case.
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

TEST_CASE(codegen_i386_emits_unary_not)
{
	auto assembly = compile_to_assembly("fn main() -> bool { return !false; }\n");

	REQUIRE(assembly.find("cmpb $0, %al") != std::string::npos);
	REQUIRE(assembly.find("sete %al") != std::string::npos);
	REQUIRE(assembly.find("movzbl %al, %eax") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_short_circuit_and)
{
	auto assembly = compile_to_assembly(
	    "extern fn fail() -> bool;\n"
	    "fn main() -> bool { return false && fail(); }\n");

	auto branch = assembly.find("je .L_logic_false_");
	auto call = assembly.find("call fail");
	REQUIRE(branch != std::string::npos);
	REQUIRE(call != std::string::npos);
	REQUIRE(branch < call);
	REQUIRE(assembly.find(".L_logic_false_") != std::string::npos);
	REQUIRE(assembly.find(".L_logic_end_") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_short_circuit_or)
{
	auto assembly = compile_to_assembly(
	    "extern fn fail() -> bool;\n"
	    "fn main() -> bool { return true || fail(); }\n",
	    rexc::CodegenTarget::X86_64);

	auto branch = assembly.find("jne .L_logic_true_");
	auto call = assembly.find("call fail");
	REQUIRE(branch != std::string::npos);
	REQUIRE(call != std::string::npos);
	REQUIRE(branch < call);
	REQUIRE(assembly.find(".L_logic_true_") != std::string::npos);
	REQUIRE(assembly.find(".L_logic_end_") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_narrow_unsigned_cast)
{
	auto assembly = compile_to_assembly(
	    "fn main() -> u8 { let x: u32 = 300; return x as u8; }\n");

	REQUIRE(assembly.find("movzbl %al, %eax") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_char_as_u32_cast)
{
	auto assembly = compile_to_assembly("fn main() -> u32 { return 'A' as u32; }\n");

	REQUIRE(assembly.find("movl $65, %eax") != std::string::npos);
	REQUIRE(assembly.find("movz") == std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_i32_cast_sign_extension)
{
	auto assembly = compile_to_assembly(
	    "fn main() -> i32 { let x: i64 = 4294967295; return x as i32; }\n",
	    rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("movslq %eax, %rax") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_pointer_address_deref_and_store)
{
	auto assembly = compile_to_assembly(
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; *p = 9; return *p; }\n");

	REQUIRE(assembly.find("leal -4(%ebp), %eax") != std::string::npos);
	REQUIRE(assembly.find("movl %ecx, (%eax)") != std::string::npos);
	REQUIRE(assembly.find("movl (%eax), %eax") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_pointer_address_deref_and_store)
{
	auto assembly = compile_to_assembly(
	    "fn main() -> i64 { let mut x: i64 = 7; let p: *i64 = &x; *p = 9; return *p; }\n",
	    rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("leaq -8(%rbp), %rax") != std::string::npos);
	REQUIRE(assembly.find("movq %rcx, (%rax)") != std::string::npos);
	REQUIRE(assembly.find("movq (%rax), %rax") != std::string::npos);
}

TEST_CASE(codegen_i386_scales_pointer_arithmetic_and_indexing)
{
	auto assembly = compile_to_assembly(
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; return p[1]; }\n");

	REQUIRE(assembly.find("imull $4, %ecx") != std::string::npos);
	REQUIRE(assembly.find("addl %ecx, %eax") != std::string::npos);
	REQUIRE(assembly.find("movl (%eax), %eax") != std::string::npos);
}

TEST_CASE(codegen_x86_64_scales_pointer_arithmetic_and_indexing)
{
	auto assembly = compile_to_assembly(
	    "fn main() -> i64 { let mut x: i64 = 7; let p: *i64 = &x; return p[1]; }\n",
	    rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("imulq $8, %rcx") != std::string::npos);
	REQUIRE(assembly.find("addq %rcx, %rax") != std::string::npos);
	REQUIRE(assembly.find("movq (%rax), %rax") != std::string::npos);
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

TEST_CASE(codegen_i386_emits_break_and_continue_jumps)
{
	auto assembly = compile_to_assembly(
	    "fn main() -> i32 { while true { continue; break; } return 0; }\n");

	REQUIRE(assembly.find(".L_while_start_") != std::string::npos);
	REQUIRE(assembly.find(".L_while_end_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_while_start_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_while_end_") != std::string::npos);
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
	REQUIRE(assembly.find("leaq .Lstr0(%rip), %rax") != std::string::npos);
	REQUIRE(assembly.find("movabsq $18446744073709551615, %rax") != std::string::npos);
	REQUIRE(assembly.find("xorq %rdx, %rdx") != std::string::npos);
	REQUIRE(assembly.find("divq %rcx") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_call_statement)
{
	auto assembly = compile_to_assembly("fn main() -> i32 { println(\"hello\"); return 0; }\n");

	REQUIRE(assembly.find("call println") != std::string::npos);
	REQUIRE(assembly.find("addl $4, %esp") != std::string::npos);
	REQUIRE(assembly.find(".section .rodata") != std::string::npos);
	REQUIRE(assembly.find(".Lstr0:") != std::string::npos);
	REQUIRE(assembly.find(".asciz \"hello\"") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_call_statement)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i64 { println(\"hello\"); return 0; }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("call println") != std::string::npos);
	REQUIRE(assembly.find("popq %rdi") != std::string::npos);
	REQUIRE(assembly.find(".section .rodata") != std::string::npos);
	REQUIRE(assembly.find(".Lstr0:") != std::string::npos);
	REQUIRE(assembly.find(".asciz \"hello\"") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_std_string_helper_calls)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { if str_eq(\"hi\", \"hi\") && str_starts_with(\"hello\", \"he\") && str_ends_with(\"hello\", \"lo\") && str_contains(\"hello\", \"ell\") && str_find(\"hello\", \"ell\") == 1 && !str_is_empty(\"hello\") { return strlen(\"hello\"); } return 0; }\n");

	REQUIRE(assembly.find("call str_eq") != std::string::npos);
	REQUIRE(assembly.find("call str_starts_with") != std::string::npos);
	REQUIRE(assembly.find("call str_ends_with") != std::string::npos);
	REQUIRE(assembly.find("call str_contains") != std::string::npos);
	REQUIRE(assembly.find("call str_find") != std::string::npos);
	REQUIRE(assembly.find("call str_is_empty") != std::string::npos);
	REQUIRE(assembly.find("call strlen") != std::string::npos);
	REQUIRE(assembly.find("addl $8, %esp") != std::string::npos);
	REQUIRE(assembly.find("addl $4, %esp") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_std_string_helper_calls)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { if str_eq(\"hi\", \"hi\") && str_starts_with(\"hello\", \"he\") && str_ends_with(\"hello\", \"lo\") && str_contains(\"hello\", \"ell\") && str_find(\"hello\", \"ell\") == 1 && !str_is_empty(\"hello\") { return strlen(\"hello\"); } return 0; }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("call str_eq") != std::string::npos);
	REQUIRE(assembly.find("call str_starts_with") != std::string::npos);
	REQUIRE(assembly.find("call str_ends_with") != std::string::npos);
	REQUIRE(assembly.find("call str_contains") != std::string::npos);
	REQUIRE(assembly.find("call str_find") != std::string::npos);
	REQUIRE(assembly.find("call str_is_empty") != std::string::npos);
	REQUIRE(assembly.find("call strlen") != std::string::npos);
	REQUIRE(assembly.find("popq %rdi") != std::string::npos);
	REQUIRE(assembly.find("popq %rsi") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_std_numeric_helper_calls)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { print_i32(42); println_i32(parse_i32(\"-7\")); return read_i32(); }\n");

	REQUIRE(assembly.find("call print_i32") != std::string::npos);
	REQUIRE(assembly.find("call println_i32") != std::string::npos);
	REQUIRE(assembly.find("call parse_i32") != std::string::npos);
	REQUIRE(assembly.find("call read_i32") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_std_numeric_helper_calls)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { print_i32(42); println_i32(parse_i32(\"-7\")); return read_i32(); }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("call print_i32") != std::string::npos);
	REQUIRE(assembly.find("call println_i32") != std::string::npos);
	REQUIRE(assembly.find("call parse_i32") != std::string::npos);
	REQUIRE(assembly.find("call read_i32") != std::string::npos);
}

TEST_CASE(codegen_x86_emits_std_panic_call)
{
	auto assembly = compile_to_assembly("fn main() -> i32 { return panic(\"boom\"); }\n");

	REQUIRE(assembly.find("call panic") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_core_memory_helper_calls)
{
	auto assembly = compile_to_assembly(
		"static mut A: [u8; 16];\n"
		"static mut B: [u8; 16];\n"
		"fn main() -> i32 { return memset_u8(A + 0, 120 as u8, 4) + memcpy_u8(B + 0, A + 0, 4) + str_copy_to(B + 0, \"hello\", 16); }\n");

	REQUIRE(assembly.find("call memset_u8") != std::string::npos);
	REQUIRE(assembly.find("call memcpy_u8") != std::string::npos);
	REQUIRE(assembly.find("call str_copy_to") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_static_i32_scalar_load_and_store)
{
	auto assembly = compile_to_assembly(
		"static mut ALLOC_OFFSET: i32 = 0;\n"
		"fn bump() -> i32 { ALLOC_OFFSET = ALLOC_OFFSET + 1; return ALLOC_OFFSET; }\n");

	REQUIRE(assembly.find(".Lstatic_ALLOC_OFFSET:") != std::string::npos);
	REQUIRE(assembly.find(".long 0") != std::string::npos);
	REQUIRE(assembly.find("movl .Lstatic_ALLOC_OFFSET, %eax") != std::string::npos);
	REQUIRE(assembly.find("movl %eax, .Lstatic_ALLOC_OFFSET") != std::string::npos);
}

TEST_CASE(codegen_i386_emits_u8_pointer_to_str_cast_as_noop)
{
	auto assembly = compile_to_assembly(
		"static mut BUFFER: [u8; 16];\nfn main() -> str { return (BUFFER + 0) as str; }\n");

	REQUIRE(assembly.find(".Lstatic_BUFFER") != std::string::npos);
	REQUIRE(assembly.find("call") == std::string::npos);
}

TEST_CASE(codegen_i386_emits_alloc_helper_calls)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { alloc_reset(); let p: *u8 = alloc_bytes(8); memset_u8(p, 65 as u8, 8); let copied: str = alloc_str_copy(\"hello\"); if str_eq(copied, \"hello\") { return alloc_remaining(); } return 0; }\n");

	REQUIRE(assembly.find("call alloc_reset") != std::string::npos);
	REQUIRE(assembly.find("call alloc_bytes") != std::string::npos);
	REQUIRE(assembly.find("call alloc_str_copy") != std::string::npos);
	REQUIRE(assembly.find("call alloc_remaining") != std::string::npos);
}
