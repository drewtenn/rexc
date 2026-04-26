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
	REQUIRE(assembly.find("str x1, [x0]") != std::string::npos);
	REQUIRE(assembly.find("ldr x0, [x0]") != std::string::npos);
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
