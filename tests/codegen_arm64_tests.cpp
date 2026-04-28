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
	rexc::SemanticOptions semantic_options;
	semantic_options.stdlib_symbols = rexc::StdlibSymbolPolicy::DefaultPrelude;
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics, semantic_options).ok());
	rexc::LowerOptions lower_options;
	lower_options.stdlib_symbols = rexc::LowerStdlibSymbolPolicy::DefaultPrelude;
	auto result =
		rexc::emit_arm64_macos_assembly(rexc::lower_to_ir(parsed.module(), lower_options), diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
	return result.assembly();
}

static std::string compile_to_arm64_assembly_with_all_stdlib_symbols(const std::string &text)
{
	rexc::SourceFile source("test.rx", text);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());

	rexc::SemanticOptions semantic_options;
	semantic_options.stdlib_symbols = rexc::StdlibSymbolPolicy::All;
	auto sema = rexc::analyze_module(parsed.module(), diagnostics, semantic_options);
	REQUIRE(sema.ok());

	rexc::LowerOptions lower_options;
	lower_options.stdlib_symbols = rexc::LowerStdlibSymbolPolicy::All;
	auto ir = rexc::lower_to_ir(parsed.module(), lower_options);
	auto result = rexc::emit_arm64_macos_assembly(ir, diagnostics);
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

TEST_CASE(codegen_arm64_macos_emits_remainder)
{
	auto assembly = compile_to_arm64_assembly("fn main() -> i32 { return 7 % 3; }\n");

	REQUIRE(assembly.find("sdiv x9, x1, x0") != std::string::npos);
	REQUIRE(assembly.find("msub x0, x9, x0, x1") != std::string::npos);
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

TEST_CASE(codegen_arm64_macos_emits_for_loop_continue_to_increment)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { let mut total: i32 = 0; for let mut i: i32 = 0; i < 3; i = i + 1 { if i == 1 { continue; } total = total + i; } return total; }\n");

	REQUIRE(assembly.find("L_for_condition_") != std::string::npos);
	REQUIRE(assembly.find("L_for_increment_") != std::string::npos);
	REQUIRE(assembly.find("L_for_end_") != std::string::npos);
	REQUIRE(assembly.find("cbz w0, L_for_end_") != std::string::npos);
	REQUIRE(assembly.find("b L_for_increment_") != std::string::npos);
	REQUIRE(assembly.find("b L_for_condition_") != std::string::npos);
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
		"static mut USER_BUFFER: [u8; 1024];\nfn main() -> str { return USER_BUFFER; }\n");

	REQUIRE(assembly.find(".zerofill __DATA,__bss,Lstatic_USER_BUFFER,1024,4") != std::string::npos);
	REQUIRE(assembly.find("adrp x0, Lstatic_USER_BUFFER@PAGE") != std::string::npos);
	REQUIRE(assembly.find("add x0, x0, Lstatic_USER_BUFFER@PAGEOFF") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_initialized_static_i32_array)
{
	auto assembly = compile_to_arm64_assembly(
		"static DAYS: [i32; 3] = [31, 28, 31];\n"
		"fn main() -> i32 { return DAYS[1]; }\n");

	REQUIRE(assembly.find("Lstatic_DAYS:") != std::string::npos);
	REQUIRE(assembly.find(".long 31") != std::string::npos);
	REQUIRE(assembly.find(".long 28") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_initialized_static_str_array)
{
	auto assembly = compile_to_arm64_assembly(
		"static MONTHS: [str; 2] = [\"Jan\", \"Feb\"];\n"
		"fn main() -> str { return MONTHS[1]; }\n");

	REQUIRE(assembly.find("Lstatic_MONTHS:") != std::string::npos);
	REQUIRE(assembly.find("Lstaticstr_MONTHS_0:") != std::string::npos);
	REQUIRE(assembly.find(".asciz \"Jan\"") != std::string::npos);
	REQUIRE(assembly.find(".quad Lstaticstr_MONTHS_0") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_prefix_and_postfix_increment_decrement)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { let mut i: i32 = 0; ++i; i++; --i; i--; return i; }\n");

	REQUIRE(assembly.find("add x0, x0, #1") != std::string::npos);
	REQUIRE(assembly.find("sub x0, x0, #1") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_match_statement)
{
	auto assembly = compile_to_arm64_assembly(
	    "fn main() -> i32 { let mut value: i32 = 2; match value { 1 => { value = 10; } 2 => { value = 20; } _ => { value = 30; } } return value; }\n");

	REQUIRE(assembly.find("L_match_arm_") != std::string::npos);
	REQUIRE(assembly.find("L_match_end_") != std::string::npos);
	REQUIRE(assembly.find("cmp x0, #1") != std::string::npos);
	REQUIRE(assembly.find("cmp x0, #2") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_match_arm_with_multiple_patterns)
{
	auto assembly = compile_to_arm64_assembly(
	    "fn main() -> i32 { let mut value: i32 = 2; match value { 1 | 2 => { value = 10; } _ => { value = 30; } } return value; }\n");

	REQUIRE(assembly.find("L_match_arm_") != std::string::npos);
	REQUIRE(assembly.find("cmp x0, #1") != std::string::npos);
	REQUIRE(assembly.find("cmp x0, #2") != std::string::npos);
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
		"fn main() -> i32 { print_i32(42); println_i32(parse_i32(\"-7\")); print_bool(true); println_bool(false); print_char('x'); println_char('y'); if parse_bool(\"true\") && !read_bool() { return 0; } return read_i32(); }\n");

	REQUIRE(assembly.find("bl _print_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _println_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _print_bool") != std::string::npos);
	REQUIRE(assembly.find("bl _println_bool") != std::string::npos);
	REQUIRE(assembly.find("bl _print_char") != std::string::npos);
	REQUIRE(assembly.find("bl _println_char") != std::string::npos);
	REQUIRE(assembly.find("bl _parse_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _read_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _parse_bool") != std::string::npos);
	REQUIRE(assembly.find("bl _read_bool") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_std_panic_call)
{
	auto assembly = compile_to_arm64_assembly("fn main() -> i32 { return panic(\"boom\"); }\n");

	REQUIRE(assembly.find("bl _panic") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_core_memory_helper_calls)
{
	auto assembly = compile_to_arm64_assembly_with_all_stdlib_symbols(
		"static mut A: [u8; 16];\n"
		"static mut B: [u8; 16];\n"
		"fn main() -> i32 { return memset_u8(A + 0, 120 as u8, 4) + memcpy_u8(B + 0, A + 0, 4) + str_copy_to(B + 0, \"hello\", 16); }\n");

	REQUIRE(assembly.find("bl _memset_u8") != std::string::npos);
	REQUIRE(assembly.find("bl _memcpy_u8") != std::string::npos);
	REQUIRE(assembly.find("bl _str_copy_to") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_static_i32_scalar_load_and_store)
{
	auto assembly = compile_to_arm64_assembly(
		"static mut USER_COUNTER: i32 = 0;\n"
		"fn bump() -> i32 { USER_COUNTER = USER_COUNTER + 1; return USER_COUNTER; }\n");

	REQUIRE(assembly.find("Lstatic_USER_COUNTER:") != std::string::npos);
	REQUIRE(assembly.find(".long 0") != std::string::npos);
	REQUIRE(assembly.find("ldr w0, [x") != std::string::npos);
	REQUIRE(assembly.find("str w0, [x") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_u8_pointer_to_str_cast_as_noop)
{
	auto assembly = compile_to_arm64_assembly(
		"static mut BUFFER: [u8; 16];\nfn main() -> str { return (BUFFER + 0) as str; }\n");

	REQUIRE(assembly.find("Lstatic_BUFFER") != std::string::npos);
	REQUIRE(assembly.find("adrp x0, Lstatic_BUFFER@PAGE") != std::string::npos);
	REQUIRE(assembly.find("\tbl _") == std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_alloc_helper_calls)
{
	auto assembly = compile_to_arm64_assembly_with_all_stdlib_symbols(
		"fn main() -> i32 { alloc_reset(); let p: *u8 = alloc_bytes(8); memset_u8(p, 65 as u8, 8); let copied: str = alloc_str_copy(\"hello\"); let owned: owned_str = owned_str_clone(copied); let joined: owned_str = owned_str_concat(owned, \"!\"); let vec: vec<i32> = vec_i32_new(2); vec_i32_push(vec, 7); let slice: slice<i32> = slice_i32_from(vec as *i32 + 2, vec_i32_len(vec)); let result: Result<i32> = result_i32_ok(slice_i32_get_or(slice, 0, -1)); let number: str = alloc_i32_to_str(result_i32_value_or(result, -42)); let truth: str = alloc_bool_to_str(result_i32_is_ok(result)); let letter: str = alloc_char_to_str('z'); if str_eq(joined, \"hello!\") && str_eq(number, \"7\") && str_eq(truth, \"true\") && str_eq(letter, \"z\") { return alloc_remaining(); } return 0; }\n");

	REQUIRE(assembly.find("bl _alloc_reset") != std::string::npos);
	REQUIRE(assembly.find("bl _alloc_bytes") != std::string::npos);
	REQUIRE(assembly.find("bl _alloc_str_copy") != std::string::npos);
	REQUIRE(assembly.find("bl _owned_str_concat") != std::string::npos);
	REQUIRE(assembly.find("bl _slice_i32_from") != std::string::npos);
	REQUIRE(assembly.find("bl _result_i32_ok") != std::string::npos);
	REQUIRE(assembly.find("bl _alloc_i32_to_str") != std::string::npos);
	REQUIRE(assembly.find("bl _alloc_bool_to_str") != std::string::npos);
	REQUIRE(assembly.find("bl _alloc_char_to_str") != std::string::npos);
	REQUIRE(assembly.find("bl _alloc_remaining") != std::string::npos);
}
