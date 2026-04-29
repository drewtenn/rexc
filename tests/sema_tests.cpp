// Semantic analysis tests for names, types, calls, and literal ranges.
//
// These tests feed real parsed modules into sema and assert whether analysis
// accepts or rejects them. They cover the language rules that are grammatical
// but not meaningful until names and primitive types are checked.
#include "rexc/diagnostics.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <string>

static rexc::SemanticResult analyze(const std::string &text,
                                    rexc::Diagnostics &diagnostics,
                                    rexc::SemanticOptions options = {})
{
	// FE-013: existing sema tests pre-date the unsafe-block rule, so the
	// helper defaults enforcement off. Tests that target FE-013 set it
	// back to true explicitly.
	options.enforce_unsafe_blocks = false;
	rexc::SourceFile source("test.rx", text);
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	return rexc::analyze_module(parsed.module(), diagnostics, options);
}

static rexc::SemanticResult analyze_strict(const std::string &text,
                                           rexc::Diagnostics &diagnostics,
                                           rexc::SemanticOptions options = {})
{
	options.enforce_unsafe_blocks = true;
	rexc::SourceFile source("test.rx", text);
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	return rexc::analyze_module(parsed.module(), diagnostics, options);
}

TEST_CASE(sema_accepts_valid_add_program)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn add(a: i32, b: i32) -> i32 { return a + b; }\n", diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_public_user_module_qualified_call)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod math { pub fn add(a: i32, b: i32) -> i32 { return a + b; } }\n"
	    "fn main() -> i32 { return math::add(1, 2); }\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_private_child_function_call)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod math { fn add(a: i32, b: i32) -> i32 { return a + b; } }\n"
	    "fn main() -> i32 { return math::add(1, 2); }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("private function 'math::add'") !=
	        std::string::npos);
}

TEST_CASE(sema_accepts_private_call_within_same_module)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod math {\n"
	    "  fn add(a: i32, b: i32) -> i32 { return a + b; }\n"
	    "  fn run() -> i32 { return add(1, 2); }\n"
	    "}\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_private_static_within_same_module)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod state {\n"
	    "  static mut VALUE: i32 = 7;\n"
	    "  fn read() -> i32 { return VALUE; }\n"
	    "}\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_descendant_access_to_ancestor_private_static)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod outer {\n"
	    "  static mut VALUE: i32 = 7;\n"
	    "  mod inner {\n"
	    "    use outer::VALUE;\n"
	    "    fn read() -> i32 { return VALUE; }\n"
	    "  }\n"
	    "}\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_descendant_call_to_ancestor_private_function)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod outer {\n"
	    "  fn base() -> i32 { return 7; }\n"
	    "  mod inner { fn run() -> i32 { return outer::base(); } }\n"
	    "}\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_parent_access_to_private_child_static)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod child { static mut VALUE: i32 = 3; }\n"
	    "use child::VALUE;\n"
	    "fn main() -> i32 { return VALUE; }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("private static 'child::VALUE'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_sibling_access_to_private_static)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod outer {\n"
	    "  mod state { static mut VALUE: i32 = 7; }\n"
	    "  mod reader {\n"
	    "    use outer::state::VALUE;\n"
	    "    fn read() -> i32 { return VALUE; }\n"
	    "  }\n"
	    "}\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("private module 'outer::state'") !=
	        std::string::npos);
}

TEST_CASE(sema_accepts_user_module_use_alias)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod math { pub fn add(a: i32, b: i32) -> i32 { return a + b; } }\n"
	    "use math::add;\n"
	    "fn main() -> i32 { return add(1, 2); }\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_use_of_private_function)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod math { fn add(a: i32, b: i32) -> i32 { return a + b; } }\n"
	    "use math::add;\n"
	    "fn main() -> i32 { return add(1, 2); }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("private function 'math::add'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_private_import_without_unknown_alias_cascade)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod math { fn add(a: i32, b: i32) -> i32 { return a + b; } }\n"
	    "use math::add;\n"
	    "fn main() -> i32 { return add(1, 2); }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	REQUIRE(formatted.find("private function 'math::add'") != std::string::npos);
	REQUIRE(formatted.find("unknown function 'add'") == std::string::npos);
}

TEST_CASE(sema_rejects_private_module_segment_from_sibling)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod outer {\n"
	    "  mod inner { pub fn value() -> i32 { return 1; } }\n"
	    "  mod sibling { fn run() -> i32 { return outer::inner::value(); } }\n"
	    "}\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("private module 'outer::inner'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_pub_function_inside_private_module_from_outside)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod outer {\n"
	    "  mod inner { pub fn value() -> i32 { return 1; } }\n"
	    "}\n"
	    "fn main() -> i32 { return outer::inner::value(); }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("private module 'outer::inner'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_use_alias_for_private_module)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod outer {\n"
	    "  mod inner { pub fn value() -> i32 { return 1; } }\n"
	    "}\n"
	    "use outer::inner;\n"
	    "fn main() -> i32 { return inner::value(); }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("private module 'outer::inner'") !=
	        std::string::npos);
}

TEST_CASE(sema_accepts_pub_module_with_pub_function_from_sibling)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod outer {\n"
	    "  pub mod inner { pub fn value() -> i32 { return 1; } }\n"
	    "  mod sibling { fn run() -> i32 { return outer::inner::value(); } }\n"
	    "}\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_pub_function_through_pub_module_alias)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod outer {\n"
	    "  pub mod inner { pub fn value() -> i32 { return 1; } }\n"
	    "}\n"
	    "use outer::inner;\n"
	    "fn main() -> i32 { return inner::value(); }\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_std_module_path_call)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { std::io::println(\"hi\"); return 0; }\n",
	                      diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_unknown_import)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("use math::missing;\nfn main() -> i32 { return 0; }\n",
	                      diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown import 'math::missing'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_duplicate_functions)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { return 0; }\nfn main() -> i32 { return 1; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate function 'main'") != std::string::npos);
}

TEST_CASE(sema_rejects_duplicate_extern_then_user_function)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("extern fn f() -> i32;\nfn f() -> i32 { return 0; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate function 'f'") != std::string::npos);
}

TEST_CASE(sema_rejects_duplicate_user_then_extern_function)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn f() -> i32 { return 0; }\nextern fn f() -> i32;\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate function 'f'") != std::string::npos);
}

TEST_CASE(sema_rejects_unknown_call)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { return missing(); }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown function 'missing'") != std::string::npos);
}

TEST_CASE(sema_rejects_wrong_arity)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn add(a: i32) -> i32 { return a; }\nfn main() -> i32 { return add(1, 2); }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("expected 1 arguments but got 2") != std::string::npos);
}

TEST_CASE(sema_rejects_unknown_local)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { return value; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown name 'value'") != std::string::npos);
}

TEST_CASE(sema_rejects_unknown_local_inside_unary)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { return -missing; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown name 'missing'") != std::string::npos);
}

TEST_CASE(sema_rejects_self_referential_let_initializer)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { let x: i32 = x; return x; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown name 'x'") != std::string::npos);
}

TEST_CASE(sema_keeps_local_visible_after_its_initializer)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { let x: i32 = 1; return x; }\n", diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_core_primitive_literals)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { let a: i8 = -1; let b: u8 = 255; let c: bool = false; let d: char = 'x'; let e: str = \"ok\"; return 0; }\n",
		diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_string_byte_indexing)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { let value: str = \"ok\"; let byte: u8 = value[1]; return byte as i32; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_static_mut_byte_buffer_as_str_storage)
{
	rexc::Diagnostics diagnostics;
	rexc::SemanticOptions options;
	options.stdlib_symbols = rexc::StdlibSymbolPolicy::None;
	auto result = analyze(
		"static mut READ_LINE_BUFFER: [u8; 1024];\n"
		"extern fn sys_read(fd: i32, buffer: *u8, len: i32) -> i32;\n"
		"fn use_buffer() -> str { let mut index: i32 = 0; let count: i32 = sys_read(0, READ_LINE_BUFFER + index, 1); *(READ_LINE_BUFFER + index) = 0; return READ_LINE_BUFFER; }\n",
		diagnostics,
		options);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_initialized_static_string_array)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"static MONTHS: [str; 3] = [\"Jan\", \"Feb\", \"Mar\"];\n"
		"fn main() -> str { return MONTHS[1]; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_initialized_static_array_type_mismatch)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"static VALUES: [i32; 2] = [1, \"bad\"];\n"
		"fn main() -> i32 { return VALUES[0]; }\n",
		diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("static array initializer type mismatch") !=
	        std::string::npos);
}

TEST_CASE(sema_accepts_static_mut_i32_scalar)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"static mut USER_COUNTER: i32 = 0;\n"
		"fn bump() -> i32 { USER_COUNTER = USER_COUNTER + 1; return USER_COUNTER; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_static_mut_i32_buffer_as_pointer_storage)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"static mut OFFSETS: [i32; 8];\n"
		"fn main() -> i32 { *(OFFSETS + 0) = 42; return OFFSETS[0]; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_non_integer_string_index)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { let value: str = \"ok\"; return value[\"bad\"] as i32; }\n",
		diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("index expression requires integer index") != std::string::npos);
}

TEST_CASE(sema_rejects_initializer_type_mismatch)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let ok: bool = 1; return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("initializer type mismatch: expected 'bool' but got 'i32'") != std::string::npos);
}

TEST_CASE(sema_rejects_arithmetic_on_bool)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: bool = true + false; return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("arithmetic requires integer operands") != std::string::npos);
}

TEST_CASE(sema_rejects_mixed_integer_arithmetic)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let a: i16 = 1; let b: i32 = 2; return a + b; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("arithmetic operands must have the same type") != std::string::npos);
}

TEST_CASE(sema_rejects_unsigned_unary_negation)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: u8 = -1; return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unary '-' requires a signed integer operand") != std::string::npos);
}

TEST_CASE(sema_rejects_out_of_range_integer_literals)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: u8 = 256; return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("integer literal does not fit type 'u8'") != std::string::npos);
}

TEST_CASE(sema_accepts_u64_max_integer_literal)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: u64 = 18446744073709551615; return 0; }\n", diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_integer_literal_above_u64_max)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: u64 = 18446744073709551616; return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("integer literal does not fit type 'u64'") != std::string::npos);
}

TEST_CASE(sema_rejects_integer_literal_above_i64_max)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: i64 = 9223372036854775808; return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("integer literal does not fit type 'i64'") != std::string::npos);
}

TEST_CASE(sema_rejects_integer_literal_below_i64_min)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: i64 = -9223372036854775809; return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("integer literal does not fit type 'i64'") != std::string::npos);
}

TEST_CASE(sema_accepts_i64_min_integer_literal)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: i64 = -9223372036854775808; return 0; }\n", diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_integer_comparisons_as_bool)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> bool { let a: i32 = 1; let b: i32 = 2; return a <= b; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_mixed_integer_comparisons)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> bool { let a: i16 = 1; let b: i32 = 2; return a < b; }\n",
	                      diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("comparison operands must have the same type") !=
	        std::string::npos);
}

TEST_CASE(sema_accepts_boolean_operators)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> bool { return !false && (1 < 2 || false); }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_unary_not_on_non_bool)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> bool { return !1; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unary '!' requires a bool operand") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_logical_operator_on_non_bool)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> bool { return true && 1; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("logical operator requires bool operands") !=
	        std::string::npos);
}

TEST_CASE(sema_accepts_default_prelude_print_functions)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { print(\"hello\"); println(\"world\"); return 0; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_default_prelude_read_line)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { let name: str = read_line(); println(name); return 0; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_default_prelude_string_helpers)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 {\n"
		"  let n: i32 = strlen(\"hello\");\n"
		"  if str_eq(\"a\", \"a\") && str_starts_with(\"hello\", \"he\") && str_ends_with(\"hello\", \"lo\") && str_contains(\"hello\", \"ell\") && str_find(\"hello\", \"ell\") == 1 && !str_is_empty(\"hello\") { return n; } else { return 0; }\n"
		"}\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_default_prelude_wrong_argument_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { println(1); return 0; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("argument type mismatch: expected 'str' but got 'i32'") != std::string::npos);
}

TEST_CASE(sema_rejects_default_prelude_strlen_wrong_argument_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { return strlen(7); }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("argument type mismatch: expected 'str' but got 'i32'") != std::string::npos);
}

TEST_CASE(sema_rejects_default_prelude_str_eq_wrong_argument_count)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { if str_eq(\"a\") { return 1; } return 0; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("function 'str_eq' expected 2 arguments but got 1") != std::string::npos);
}

TEST_CASE(sema_accepts_default_prelude_numeric_helpers)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 {\n"
		"  print_i32(42);\n"
		"  println_i32(parse_i32(\"-7\"));\n"
		"  print_bool(true);\n"
		"  println_bool(false);\n"
		"  print_char('x');\n"
		"  println_char('y');\n"
		"  if parse_bool(\"true\") && !read_bool() { return 0; }\n"
		"  return read_i32();\n"
		"}\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_default_prelude_panic)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { return panic(\"boom\"); }\n", diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_non_default_stdlib_helpers_as_bare_names)
{
	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "static mut BUFFER: [u8; 16];\n"
		    "fn main() -> i32 { return memset_u8(BUFFER + 0, 0 as u8, 16); }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("unknown function 'memset_u8'") !=
		        std::string::npos);
	}

	// FE-106: `alloc_reset` / `alloc_remaining` are now part of the
	// default prelude alongside `alloc_bytes` and the `arena_*` family,
	// so they are intentionally NOT covered by this rejection test
	// anymore. The internal helpers below remain out of the prelude.

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "fn main() -> i32 { let value: str = alloc_i32_to_str(7); println(value); return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("unknown function 'alloc_i32_to_str'") !=
		        std::string::npos);
	}
}

TEST_CASE(sema_accepts_explicit_std_bridge_path_without_bare_bridge_name)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "fn main() -> i32 { std::io::println(\"hi\"); return 0; }\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_user_module_that_collides_with_stdlib_root)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod std { fn value() -> i32 { return 0; } }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate module 'std'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_user_module_that_collides_with_stdlib_bridge_module)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "mod std { mod io { fn value() -> i32 { return 0; } } }\n"
	    "fn main() -> i32 { std::io::println(\"hi\"); return 0; }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate module 'std'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_std_bridge_symbol_as_bare_name)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "fn main() -> i32 { std_io_println(\"hi\"); return 0; }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown function 'std_io_println'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_user_function_that_collides_with_hidden_stdlib_symbol)
{
	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "fn std_io_println(value: str) -> i32 { return 0; }\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate function 'std_io_println'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "fn alloc_reset() -> i32 { return 0; }\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate function 'alloc_reset'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "fn _str_contains_at(value: str, needle: str, start: i32) -> bool { return false; }\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate function '_str_contains_at'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "fn sys_write(fd: i32, buffer: str, len: i32) -> i32 { return 0; }\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate function 'sys_write'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "extern fn sys_write(fd: i32, buffer: str, len: i32) -> i32;\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate function 'sys_write'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "extern fn __rexc_empty_string() -> i32;\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate function '__rexc_empty_string'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "mod std_io { pub fn println(value: str) -> i32 { return 0; } }\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate function 'std_io_println'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "mod sys { pub extern fn write(fd: i32, buffer: str, len: i32) -> i32; }\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate function 'sys_write'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "mod std_io { pub static mut println: i32 = 0; }\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate static 'std_io_println'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "static mut READ_LINE_BUFFER: [u8; 1024];\n"
		    "fn main() -> i32 { return 0; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate static 'READ_LINE_BUFFER'") !=
		        std::string::npos);
	}

	{
		rexc::Diagnostics diagnostics;

		auto result = analyze(
		    "static mut ALLOC_OFFSET: i32 = 0;\n"
		    "fn main() -> i32 { return ALLOC_OFFSET; }\n",
		    diagnostics);

		REQUIRE(!result.ok());
		REQUIRE(diagnostics.format().find("duplicate static 'ALLOC_OFFSET'") !=
		        std::string::npos);
	}
}

TEST_CASE(sema_accepts_all_stdlib_core_memory_helpers_as_bare_names)
{
	rexc::Diagnostics diagnostics;
	rexc::SemanticOptions options;
	options.stdlib_symbols = rexc::StdlibSymbolPolicy::All;
	auto result = analyze(
		"static mut A: [u8; 16];\n"
		"static mut B: [u8; 16];\n"
		"fn main() -> i32 {\n"
		"  let a: i32 = memset_u8(A + 0, 120 as u8, 4);\n"
		"  let b: i32 = memcpy_u8(B + 0, A + 0, 4);\n"
		"  let c: i32 = str_copy_to(B + 0, \"hello\", 16);\n"
		"  return a + b + c;\n"
		"}\n",
		diagnostics,
		options);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_all_stdlib_alloc_helpers_as_bare_names)
{
	rexc::Diagnostics diagnostics;
	rexc::SemanticOptions options;
	options.stdlib_symbols = rexc::StdlibSymbolPolicy::All;
	auto result = analyze(
		"fn main() -> i32 {\n"
		"  alloc_reset();\n"
		"  let remaining: i32 = alloc_remaining();\n"
		"  let used: i32 = alloc_used();\n"
		"  let capacity: i32 = alloc_capacity();\n"
		"  let can_allocate: bool = alloc_can_allocate(8);\n"
		"  let p: *u8 = alloc_bytes(8);\n"
		"  memset_u8(p, 65 as u8, 8);\n"
		"  let copied: str = alloc_str_copy(\"hello\");\n"
		"  let joined: str = alloc_str_concat(copied, \"!\");\n"
		"  let clone: owned_str = owned_str_clone(joined);\n"
		"  let clone_len: i32 = owned_str_len(clone);\n"
		"  let clone_empty: bool = owned_str_is_empty(clone);\n"
		"  let owned_joined: owned_str = owned_str_concat(clone, \"!\");\n"
		"  let box: *i32 = box_i32_new(7);\n"
		"  let box_value: i32 = box_i32_read(box);\n"
		"  let written: i32 = box_i32_write(box, box_value + 1);\n"
		"  let vec: vec<i32> = vec_i32_new(2);\n"
		"  let pushed: i32 = vec_i32_push(vec, 5);\n"
		"  let vec_len: i32 = vec_i32_len(vec);\n"
		"  let vec_capacity: i32 = vec_i32_capacity(vec);\n"
		"  let vec_value: i32 = vec_i32_get_or(vec, 0, -1);\n"
		"  let slice: slice<i32> = slice_i32_from(vec as *i32 + 2, vec_len);\n"
		"  let slice_len: i32 = slice_i32_len(slice);\n"
		"  let slice_value: i32 = slice_i32_get_or(slice, 0, -1);\n"
		"  let result: Result<i32> = result_i32_ok(slice_value);\n"
		"  let result_ok: bool = result_i32_is_ok(result);\n"
		"  let result_value: i32 = result_i32_value_or(result, -1);\n"
		"  let number: str = alloc_i32_to_str(-42);\n"
		"  let truth: str = alloc_bool_to_str(true);\n"
		"  let letter: str = alloc_char_to_str('z');\n"
		"  if can_allocate && !clone_empty && result_ok && str_eq(joined, \"hello!\") && str_eq(owned_joined, \"hello!!\") && str_eq(number, \"-42\") && str_eq(truth, \"true\") && str_eq(letter, \"z\") { return remaining + used + capacity + clone_len + box_value + written + pushed + vec_len + vec_capacity + vec_value + slice_len + result_value; }\n"
		"  return 0;\n"
		"}\n",
		diagnostics,
		options);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_default_prelude_print_i32_wrong_argument_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { print_i32(\"7\"); return 0; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("argument type mismatch: expected 'i32' but got 'str'") != std::string::npos);
}

TEST_CASE(sema_rejects_default_prelude_parse_i32_wrong_argument_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { return parse_i32(7); }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("argument type mismatch: expected 'str' but got 'i32'") != std::string::npos);
}

TEST_CASE(sema_rejects_duplicate_default_prelude_function)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn println(value: str) -> i32 { return 0; }\n"
		"fn main() -> i32 { return 0; }\n",
		diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate function 'println'") != std::string::npos);
}

TEST_CASE(sema_accepts_supported_explicit_casts)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> u32 { let x: i32 = 42; let b: u8 = true as u8; let c: u32 = 'A' as u32; return x as u32 + c; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_unsupported_bool_to_str_casts)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> str { let ok: bool = true; return ok as str; }\n",
	                      diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("cannot cast 'bool' to 'str'") != std::string::npos);
}

TEST_CASE(sema_accepts_u8_pointer_to_str_cast)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"static mut BUFFER: [u8; 16];\n"
		"fn main() -> str { return (BUFFER + 0) as str; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_pointer_to_pointer_casts)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"static mut BUFFER: [u8; 16];\n"
		"fn main() -> i32 { let p: *i32 = (BUFFER + 0) as *i32; return 0; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_zero_to_pointer_and_str_casts)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { let ptr: *str = 0 as *str; let text: str = 0 as str; return ptr as i32 + text as i32; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_unsupported_char_casts)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> u8 { return 'A' as u8; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("cannot cast 'char' to 'u8'") != std::string::npos);
}

TEST_CASE(sema_accepts_pointer_address_deref_and_indirect_assignment)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; *p = 9; return *p; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_address_of_immutable_local)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: i32 = 7; let p: *i32 = &x; return x; }\n",
	                      diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("address-of requires mutable local 'x'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_deref_of_non_pointer)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: i32 = 7; return *x; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("dereference requires pointer operand") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_indirect_assignment_through_non_pointer)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let mut x: i32 = 7; *x = 9; return x; }\n",
	                      diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("indirect assignment requires pointer target") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_indirect_assignment_type_mismatch)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; *p = true; return x; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("assignment type mismatch: expected 'i32' but got 'bool'") !=
	        std::string::npos);
}

TEST_CASE(sema_accepts_pointer_arithmetic_and_indexing)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut x: i32 = 7; let p: *i32 = &x; let q: *i32 = p + 1; let r: *i32 = q - 1; return r[0]; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_pointer_arithmetic_with_non_integer_offset)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> *i32 { let mut x: i32 = 7; let p: *i32 = &x; return p + true; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("pointer arithmetic requires integer offset") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_pointer_plus_pointer)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> *i32 { let mut x: i32 = 7; let p: *i32 = &x; return p + p; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("pointer arithmetic requires integer offset") !=
	        std::string::npos);
}

TEST_CASE(sema_accepts_if_else_with_bool_condition)
{
	rexc::Diagnostics diagnostics;
	auto result =
	    analyze("fn main() -> i32 { if 1 < 2 { return 1; } else { return 0; } }\n",
	            diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_else_if_with_bool_condition)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { if 1 < 2 { return 1; } else if 2 < 3 { return 2; } else { return 0; } }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_non_bool_if_condition)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { if 1 { return 1; } return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("if condition must be bool") != std::string::npos);
}

TEST_CASE(sema_accepts_assignment_to_mutable_local_in_while_loop)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut x: i32 = 0; while x < 3 { x = x + 1; } return x; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_for_loop_and_scopes_initializer_to_loop)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut total: i32 = 0; for let mut i: i32 = 0; i < 3; i = i + 1 { total = total + i; } return total; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_prefix_and_postfix_increment_decrement)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { let mut i: i32 = 0; ++i; i++; --i; i--; return i; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_increment_in_for_header)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { let mut total: i32 = 0; for let mut i: i32 = 0; i < 3; i++ { total = total + i; } return total; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_increment_of_immutable_local)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let i: i32 = 0; i++; return i; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("cannot increment immutable local 'i'") !=
	        std::string::npos);
}

TEST_CASE(sema_accepts_match_statement_with_integer_patterns)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut value: i32 = 0; match value { 1 => { value = 10; } 2 => { value = 20; } _ => { value = 30; } } return value; }\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_match_arm_with_multiple_patterns)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut value: i32 = 0; match value { 1 | 2 => { value = 10; } _ => { value = 30; } } return value; }\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_match_pattern_type_mismatch)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut value: i32 = 0; match value { true => { value = 1; } _ => { value = 2; } } return value; }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("match pattern type mismatch") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_for_initializer_local_after_loop)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { for let mut i: i32 = 0; i < 3; i = i + 1 { } return i; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown name 'i'") != std::string::npos);
}

TEST_CASE(sema_rejects_assignment_to_immutable_local)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let x: i32 = 0; x = 1; return x; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("cannot assign to immutable local 'x'") != std::string::npos);
}

TEST_CASE(sema_rejects_assignment_to_parameter)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main(x: i32) -> i32 { x = 1; return x; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("cannot assign to immutable local 'x'") != std::string::npos);
}

TEST_CASE(sema_rejects_assignment_type_mismatch)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { let mut x: i32 = 0; x = true; return x; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("assignment type mismatch: expected 'i32' but got 'bool'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_non_bool_while_condition)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { while 1 { return 1; } return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("while condition must be bool") != std::string::npos);
}

TEST_CASE(sema_keeps_while_body_locals_scoped_to_body)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut x: i32 = 0; while x < 1 { let y: i32 = 2; x = y; } return y; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown name 'y'") != std::string::npos);
}

TEST_CASE(sema_accepts_break_and_continue_inside_loop)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let mut x: i32 = 0; while x < 10 { x = x + 1; if x == 3 { continue; } if x == 7 { break; } } return x; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_break_outside_loop)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { break; return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("break statement outside loop") != std::string::npos);
}

TEST_CASE(sema_rejects_continue_outside_loop)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { if true { continue; } return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("continue statement outside loop") != std::string::npos);
}

// FE-002b (Phase 1): struct registration + field access type-checking.
//
// These tests assert that sema recognizes struct declarations as type names,
// rejects duplicates and unknown struct uses, and resolves field accesses to
// the declared field type. They MUST fail until sema is taught about structs.

TEST_CASE(sema_accepts_struct_declared_then_used_as_param_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn read_x(p: Point) -> i32 { return p.x; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_use_of_undeclared_struct_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn read_x(p: NotDeclared) -> i32 { return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown type 'NotDeclared'") != std::string::npos);
}

TEST_CASE(sema_rejects_duplicate_struct_declaration)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "struct Point { a: i32, b: i32 }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("struct 'Point' already declared") != std::string::npos);
}

TEST_CASE(sema_rejects_field_access_on_non_struct)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let n: i32 = 0; return n.x; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("field access requires struct value") != std::string::npos);
}

TEST_CASE(sema_rejects_unknown_field_on_struct)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn read_z(p: Point) -> i32 { return p.z; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("struct 'Point' has no field 'z'") != std::string::npos);
}

TEST_CASE(sema_field_access_resolves_to_declared_field_type)
{
	// Anti-stub: distinguishes "field exists" from "field type recorded".
	// If sema returns the wrong type for `p.flag`, the assignment to a bool
	// local should fail. If it correctly returns bool, the program is OK.
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Mixed { count: i32, flag: bool }\n"
	    "fn check(m: Mixed) -> bool { let b: bool = m.flag; return b; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_field_type_mismatch_on_let_initializer)
{
	// `m.count` is i32 — assigning it to a bool local must be rejected.
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Mixed { count: i32, flag: bool }\n"
	    "fn check(m: Mixed) -> bool { let b: bool = m.count; return b; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
}

TEST_CASE(sema_accepts_field_access_via_deref_of_struct_pointer)
{
	// (*p).x where p is *Point — the existing test pattern that lets us reach
	// codegen for FE-003 without yet having struct literals.
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn read_x(p: *Point) -> i32 { return (*p).x; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_field_assign_via_deref_of_struct_pointer)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn write_x(p: *Point) -> i32 { (*p).x = 42; return 0; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_field_assign_value_type_mismatch)
{
	// Assigning a bool to an i32 field must error.
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn bad(p: *Point) -> i32 { (*p).x = true; return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("type mismatch") != std::string::npos);
}

TEST_CASE(sema_rejects_field_assign_to_unknown_field)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn bad(p: *Point) -> i32 { (*p).z = 1; return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("struct 'Point' has no field 'z'") != std::string::npos);
}

TEST_CASE(sema_accepts_struct_literal_with_named_fields)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn make() -> i32 { let p: Point = Point { x: 40, y: 2 }; return p.y; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_struct_literal_missing_field)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn bad() -> i32 { let p: Point = Point { x: 40 }; return p.x; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("missing field 'y'") != std::string::npos);
}

// FE-007 (Phase 1): tuple types, tuple expressions, and numeric field access.

TEST_CASE(sema_accepts_tuple_expression_and_index_access)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> bool { let pair: (i32, bool) = (40, true); return pair.1; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_tuple_index_out_of_range)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let pair: (i32, bool) = (40, true); return pair.2; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("tuple has no field '2'") != std::string::npos);
}

TEST_CASE(sema_rejects_tuple_element_type_mismatch)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { let pair: (i32, bool) = (40, 2); return pair.0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("tuple element type mismatch") != std::string::npos);
}

// FE-008 (Phase 1): first-class slice type, len(), and checked indexing.

TEST_CASE(sema_accepts_first_class_slice_len_and_index)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn first(xs: &[i32]) -> i32 { return len(xs) + xs[0]; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_len_on_non_slice)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn bad(value: i32) -> i32 { return len(value); }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("len() requires slice or str argument") != std::string::npos);
}

TEST_CASE(sema_rejects_negative_slice_index_with_span)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn bad(xs: &[i32]) -> i32 { return xs[-1]; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("slice index out of bounds") != std::string::npos);
	REQUIRE(diagnostics.format().find("test.rx:1:39") != std::string::npos);
}

// FE-011 (Phase 1/2 bridge): stdlib-shaped Option<T> and Result<T,E>.

TEST_CASE(sema_accepts_prelude_option_and_result_types)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum MyErr { Bad }\n"
	    "fn maybe() -> Option<i32> { return Some(7); }\n"
	    "fn empty() -> Option<i32> { return None(); }\n"
	    "fn fallible() -> Result<i32, MyErr> { return Ok(7); }\n"
	    "fn fail() -> Result<i32, MyErr> { return Err(Bad()); }\n"
	    "fn main() -> i32 {\n"
	    "  let value: Option<i32> = Some(1);\n"
	    "  let missing: Option<i32> = None();\n"
	    "  let good: Result<i32, MyErr> = Ok(2);\n"
	    "  let bad: Result<i32, MyErr> = Err(Bad());\n"
	    "  return 0;\n"
	    "}\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

// FE-012 (Phase 1): `?` operator for Result propagation.

TEST_CASE(sema_accepts_try_operator_in_result_returning_function)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn fallible() -> Result<i32> { return Ok(7); }\n"
	    "fn caller() -> Result<i32> {\n"
	    "  let value: i32 = fallible()?;\n"
	    "  return Ok(value + 1);\n"
	    "}\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_try_operator_outside_result_returning_function)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn fallible() -> Result<i32> { return Ok(7); }\n"
	    "fn caller() -> i32 {\n"
	    "  let value: i32 = fallible()?;\n"
	    "  return value;\n"
	    "}\n",
	    diagnostics);
	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	REQUIRE(formatted.find("'?' operator requires enclosing function to return Result") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_try_operator_on_non_result_operand)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn caller() -> Result<i32> {\n"
	    "  let value: i32 = 7?;\n"
	    "  return Ok(value);\n"
	    "}\n",
	    diagnostics);
	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	REQUIRE(formatted.find("'?' operator requires Result operand") !=
	        std::string::npos);
}

// FE-013 (Phase 1): `unsafe` blocks gate raw pointer deref and extern fn calls.

TEST_CASE(sema_strict_rejects_raw_deref_outside_unsafe)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze_strict(
	    "fn read(p: *i32) -> i32 { return *p; }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	REQUIRE(formatted.find("dereference of raw pointer requires `unsafe` block") !=
	        std::string::npos);
}

TEST_CASE(sema_strict_accepts_raw_deref_inside_unsafe_block)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze_strict(
	    "fn read(p: *i32) -> i32 {\n"
	    "  let mut value: i32 = 0;\n"
	    "  unsafe { value = *p; }\n"
	    "  return value;\n"
	    "}\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_strict_accepts_raw_deref_inside_unsafe_fn)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze_strict(
	    "unsafe fn read(p: *i32) -> i32 { return *p; }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_strict_rejects_indirect_assign_outside_unsafe)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze_strict(
	    "fn write(p: *i32, v: i32) -> i32 { *p = v; return 0; }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	REQUIRE(formatted.find("indirect write through raw pointer requires `unsafe` block") !=
	        std::string::npos);
}

TEST_CASE(sema_strict_rejects_field_assign_outside_unsafe)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze_strict(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn write(p: *Point) -> i32 { (*p).x = 7; return 0; }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	REQUIRE(formatted.find("field write through raw pointer requires `unsafe` block") !=
	        std::string::npos);
}

TEST_CASE(sema_strict_rejects_extern_call_outside_unsafe)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze_strict(
	    "extern fn raw_syscall(code: i32) -> i32;\n"
	    "fn main() -> i32 { return raw_syscall(0); }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	REQUIRE(formatted.find("call to extern function 'raw_syscall' requires `unsafe` block") !=
	        std::string::npos);
}

TEST_CASE(sema_strict_accepts_extern_call_inside_unsafe_block)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze_strict(
	    "extern fn raw_syscall(code: i32) -> i32;\n"
	    "fn main() -> i32 {\n"
	    "  let mut result: i32 = 0;\n"
	    "  unsafe { result = raw_syscall(0); }\n"
	    "  return result;\n"
	    "}\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_strict_unsafe_block_in_loop_supports_break_continue)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze_strict(
	    "fn scan(p: *i32) -> i32 {\n"
	    "  let mut i: i32 = 0;\n"
	    "  while i < 10 {\n"
	    "    unsafe {\n"
	    "      if *p == 0 { break; }\n"
	    "    }\n"
	    "    i = i + 1;\n"
	    "  }\n"
	    "  return i;\n"
	    "}\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

// FE-102 (Phase 2): sema resolves generic type parameters in scope.

TEST_CASE(sema_accepts_generic_identity_function)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn id<T>(x: T) -> T { return x; }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_generic_let_binding_with_type_parameter)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn passthrough<T>(x: T) -> T {\n"
	    "  let y: T = x;\n"
	    "  return y;\n"
	    "}\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_generic_struct_with_field_typed_by_parameter)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Box<T> { value: T }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_unknown_type_name_outside_generic_scope)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn use_t(x: T) -> T { return x; }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown type 'T'") != std::string::npos);
}

// FE-103 (Phase 2): generic call-site inference + diagnostics.

TEST_CASE(sema_accepts_generic_call_with_inferred_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn id<T>(x: T) -> T { return x; }\n"
	    "fn main() -> i32 {\n"
	    "  let v: i32 = id(42);\n"
	    "  return v;\n"
	    "}\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_diagnoses_inconsistent_generic_binding_at_call_site)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn pair_eq<T>(a: T, b: T) -> bool { return true; }\n"
	    "fn main() -> i32 {\n"
	    "  if pair_eq(7, true) { return 0; }\n"
	    "  return 1;\n"
	    "}\n",
	    diagnostics);
	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	// Diagnostic must point at the call site, not the template body.
	REQUIRE(formatted.find("cannot infer generic type") != std::string::npos);
}

// FE-103.1: generic param substitution must walk string-encoded nested
// template names like Box<T> in fn return types and struct fields, so the
// inferred return type at the call site mangles to Box__i32 (matching the
// consumer's annotation), not Box__T.

TEST_CASE(sema_substitutes_nested_generic_in_function_return_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Box<T> { value: T }\n"
	    "fn wrap<T>(x: T) -> Box<T> { return Box { value: x }; }\n"
	    "fn main() -> i32 {\n"
	    "  let b: Box<i32> = wrap(42);\n"
	    "  return b.value;\n"
	    "}\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_substitutes_nested_generic_in_struct_field)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Box<T> { value: T }\n"
	    "struct Pair<A, B> { left: Box<A>, right: Box<B> }\n"
	    "fn main() -> i32 {\n"
	    "  let bi: Box<i32> = Box { value: 1 };\n"
	    "  let bb: Box<bool> = Box { value: true };\n"
	    "  let p: Pair<i32, bool> = Pair { left: bi, right: bb };\n"
	    "  return p.left.value;\n"
	    "}\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

// Inside a generic function body, a value of type *Vec<T> has pointee
// name 'Vec<T>' — the unmangled instantiation form. Field access and
// assignment must look up the base template (Vec) when the direct
// lookup of 'Vec<T>' misses.
TEST_CASE(sema_resolves_generic_struct_field_through_pointer_in_generic_body)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Vec<T> { data: *T, len: i32, capacity: i32 }\n"
	    "fn vec_push<T>(v: *Vec<T>, value: T) -> i32 {\n"
	    "    if (*v).len >= (*v).capacity { return 0; }\n"
	    "    (*v).len = (*v).len + 1;\n"
	    "    return 1;\n"
	    "}\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

// FE-005 (Phase 1): enum registration, constructor checking, and tags.
//
// These tests keep enum semantics at the type-checking boundary. Lowering and
// codegen remain FE-006, but sema must know enum type names, constructors,
// variant tags, and payload signatures before lowering can be trustworthy.

TEST_CASE(sema_accepts_enum_declared_then_constructed_as_return_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32) }\n"
	    "fn make() -> Option { return Some(42); }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_qualified_payloadless_enum_constructor)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32) }\n"
	    "fn make() -> Option { return Option::None(); }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_enum_constructor_in_let_initializer)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32) }\n"
	    "fn main() -> i32 { let value: Option = Some(7); return 0; }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_enum_match_destructuring_payload_binding)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32) }\n"
	    "fn main() -> i32 { let value: Option = Some(7); match value { Some(x) => { return x; } _ => { return 0; } } }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_non_exhaustive_enum_match)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32) }\n"
	    "fn main() -> i32 { let value: Option = Some(7); match value { Some(x) => { return x; } } }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	REQUIRE(formatted.find("non-exhaustive enum match: missing variant 'None'") !=
	        std::string::npos);
	REQUIRE(formatted.find("test.rx:1:15") != std::string::npos);
}

TEST_CASE(sema_accepts_struct_match_destructuring_field_bindings)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Point { x: i32, y: i32 }\n"
	    "fn main() -> i32 { let point: Point = Point { x: 7, y: 2 }; match point { Point { x, y } => { return x + y; } } }\n",
	    diagnostics);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_enum_match_destructuring_payload_count_mismatch)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32) }\n"
	    "fn main() -> i32 { let value: Option = Some(7); match value { Some() => { return 1; } _ => { return 0; } } }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("enum variant 'Some' pattern expected 1 bindings but got 0") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_use_of_undeclared_enum_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn make() -> Option { return 0; }\n", diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown type 'Option'") != std::string::npos);
}

TEST_CASE(sema_rejects_duplicate_enum_declaration)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None }\n"
	    "enum Option { Some(i32) }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("enum 'Option' already declared") != std::string::npos);
}

TEST_CASE(sema_rejects_enum_name_that_collides_with_struct_name)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "struct Token { value: i32 }\n"
	    "enum Token { End }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("type 'Token' already declared") != std::string::npos);
}

TEST_CASE(sema_rejects_duplicate_enum_variant)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32), Some(bool) }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("variant 'Some' already declared in enum 'Option'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_unknown_enum_payload_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Holder { Value(NotDeclared) }\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown type 'NotDeclared'") != std::string::npos);
}

TEST_CASE(sema_rejects_unknown_enum_variant_for_expected_enum)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32) }\n"
	    "fn make() -> Option { return Missing(); }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown enum variant 'Missing' for enum 'Option'") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_enum_constructor_argument_count_mismatch)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32) }\n"
	    "fn make() -> Option { return Some(); }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("enum variant 'Some' expected 1 arguments but got 0") !=
	        std::string::npos);
}

TEST_CASE(sema_rejects_enum_constructor_payload_type_mismatch)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "enum Option { None, Some(i32) }\n"
	    "fn make() -> Option { return Some(true); }\n",
	    diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("enum payload type mismatch: expected 'i32' but got 'bool'") !=
	        std::string::npos);
}

// FE-105: an explicit allocator-as-value parameter — `*Arena` — must
// type-check, including calling the registered stdlib `arena_alloc`
// with a `*Arena` argument from user code. Now that
// `stdlib::resolve_source_type` recognizes user-struct names declared
// in the same parsed unit and `stdlib_structs()` exposes the Arena
// shape to sema, user code can name `Arena` directly and call into the
// registered `arena_*` functions with `StdlibSymbolPolicy::All` (the
// non-prelude allocator API has always required this opt-in, mirroring
// the existing `alloc_bytes` test above).
TEST_CASE(sema_accepts_explicit_arena_parameter)
{
	rexc::Diagnostics diagnostics;
	rexc::SemanticOptions options;
	options.stdlib_symbols = rexc::StdlibSymbolPolicy::All;
	auto result = analyze(
	    "fn allocate_block(arena: *Arena, n: i32) -> *u8 {\n"
	    "    return arena_alloc(arena, n);\n"
	    "}\n"
	    "fn main() -> i32 { return 0; }\n",
	    diagnostics, options);
	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}
