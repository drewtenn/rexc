// Unit coverage for Darwin ARM64 assembly emission.
#include "rexc/codegen_arm64.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <string>

static std::string compile_to_arm64_assembly(const std::string &text)
{
	rexc::SourceFile source("test.rx", text);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());
	auto result =
		rexc::emit_arm64_macos_assembly(rexc::lower_to_ir(parsed.module()), diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	return result.assembly();
}

TEST_CASE(codegen_arm64_macos_emits_main_skeleton)
{
	auto assembly = compile_to_arm64_assembly("fn main() -> i32 { return 42; }\n");

	REQUIRE(assembly.find(".globl _main") != std::string::npos);
	REQUIRE(assembly.find("_main:") != std::string::npos);
	REQUIRE(assembly.find("stp x29, x30, [sp, #-16]!") != std::string::npos);
	REQUIRE(assembly.find("ret") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_integer_return)
{
	auto assembly = compile_to_arm64_assembly("fn main() -> i32 { return 42; }\n");

	REQUIRE(assembly.find("mov w0, #42") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_call_and_local)
{
	auto assembly = compile_to_arm64_assembly(
		"fn add(a: i32, b: i32) -> i32 { return a + b; }\n"
		"fn main() -> i32 { let value: i32 = add(20, 22); return value; }\n");

	REQUIRE(assembly.find("bl _add") != std::string::npos);
	REQUIRE(assembly.find("str x0, [x29, #-") != std::string::npos);
	REQUIRE(assembly.find("ldr x0, [x29, #-") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_string_literal)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { let s: str = \"hi\"; return 0; }\n");

	REQUIRE(assembly.find(".cstring") != std::string::npos);
	REQUIRE(assembly.find("Lstr0:") != std::string::npos);
	REQUIRE(assembly.find(".asciz \"hi\"") != std::string::npos);
	REQUIRE(assembly.find("adrp x0, Lstr0@PAGE") != std::string::npos);
	REQUIRE(assembly.find("add x0, x0, Lstr0@PAGEOFF") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_if_else_comparison)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { if 1 < 2 { return 7; } else { return 9; } }\n");

	REQUIRE(assembly.find("cmp x1, x0") != std::string::npos);
	REQUIRE(assembly.find("cset w0, lt") != std::string::npos);
	REQUIRE(assembly.find("cbz w0, L_else_") != std::string::npos);
	REQUIRE(assembly.find("b L_end_if_") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_while_break_continue)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { let mut x: i32 = 0; while x < 3 { x = x + 1; continue; } return x; }\n");

	REQUIRE(assembly.find("L_while_start_") != std::string::npos);
	REQUIRE(assembly.find("L_while_end_") != std::string::npos);
	REQUIRE(assembly.find("b L_while_start_") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_casts)
{
	auto assembly = compile_to_arm64_assembly("fn main() -> u8 { let x: u32 = 300; return x as u8; }\n");

	REQUIRE(assembly.find("uxtb w0, w0") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_pointer_load_store)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { let mut x: i32 = 1; let p: *i32 = &x; *p = 2; return *p; }\n");

	REQUIRE(assembly.find("sub x0, x29, #") != std::string::npos);
	REQUIRE(assembly.find("str w1, [x0]") != std::string::npos);
	REQUIRE(assembly.find("ldrsw x0, [x0]") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_string_index_byte_load)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { let value: str = \"ok\"; let byte: u8 = value[1]; return byte as i32; }\n");

	REQUIRE(assembly.find("ldrb w0, [x0]") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_static_byte_buffer)
{
	auto assembly = compile_to_arm64_assembly(
		"static mut READ_LINE_BUFFER: [u8; 1024];\nfn main() -> str { return READ_LINE_BUFFER; }\n");

	REQUIRE(assembly.find(".zerofill __DATA,__bss,Lstatic_READ_LINE_BUFFER,1024,4") != std::string::npos);
	REQUIRE(assembly.find("adrp x0, Lstatic_READ_LINE_BUFFER@PAGE") != std::string::npos);
	REQUIRE(assembly.find("add x0, x0, Lstatic_READ_LINE_BUFFER@PAGEOFF") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_call_statement)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { println(\"hello\"); return 0; }\n");

	REQUIRE(assembly.find("bl _println") != std::string::npos);
	REQUIRE(assembly.find(".cstring") != std::string::npos);
	REQUIRE(assembly.find("Lstr0:") != std::string::npos);
	REQUIRE(assembly.find(".asciz \"hello\"") != std::string::npos);
	REQUIRE(assembly.find("adrp x0, Lstr0@PAGE") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_std_string_helper_calls)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { if str_eq(\"hi\", \"hi\") && str_starts_with(\"hello\", \"he\") && str_ends_with(\"hello\", \"lo\") && str_contains(\"hello\", \"ell\") && str_find(\"hello\", \"ell\") == 1 && !str_is_empty(\"hello\") { return strlen(\"hello\"); } return 0; }\n");

	REQUIRE(assembly.find("bl _str_eq") != std::string::npos);
	REQUIRE(assembly.find("bl _str_starts_with") != std::string::npos);
	REQUIRE(assembly.find("bl _str_ends_with") != std::string::npos);
	REQUIRE(assembly.find("bl _str_contains") != std::string::npos);
	REQUIRE(assembly.find("bl _str_find") != std::string::npos);
	REQUIRE(assembly.find("bl _str_is_empty") != std::string::npos);
	REQUIRE(assembly.find("bl _strlen") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_std_numeric_helper_calls)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { print_i32(42); println_i32(parse_i32(\"-7\")); return read_i32(); }\n");

	REQUIRE(assembly.find("bl _print_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _println_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _parse_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _read_i32") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_std_panic_call)
{
	auto assembly = compile_to_arm64_assembly("fn main() -> i32 { return panic(\"boom\"); }\n");

	REQUIRE(assembly.find("bl _panic") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_core_memory_helper_calls)
{
	auto assembly = compile_to_arm64_assembly(
		"static mut A: [u8; 16];\n"
		"static mut B: [u8; 16];\n"
		"fn main() -> i32 { return memset_u8(A + 0, 120 as u8, 4) + memcpy_u8(B + 0, A + 0, 4) + str_copy_to(B + 0, \"hello\", 16); }\n");

	REQUIRE(assembly.find("bl _memset_u8") != std::string::npos);
	REQUIRE(assembly.find("bl _memcpy_u8") != std::string::npos);
	REQUIRE(assembly.find("bl _str_copy_to") != std::string::npos);
}
