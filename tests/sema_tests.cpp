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

static rexc::SemanticResult analyze(const std::string &text, rexc::Diagnostics &diagnostics)
{
	rexc::SourceFile source("test.rx", text);
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	return rexc::analyze_module(parsed.module(), diagnostics);
}

TEST_CASE(sema_accepts_valid_add_program)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn add(a: i32, b: i32) -> i32 { return a + b; }\n", diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_duplicate_functions)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze("fn main() -> i32 { return 0; }\nfn main() -> i32 { return 1; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate function 'main'") != std::string::npos);
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

TEST_CASE(sema_accepts_if_else_with_bool_condition)
{
	rexc::Diagnostics diagnostics;
	auto result =
	    analyze("fn main() -> i32 { if 1 < 2 { return 1; } else { return 0; } }\n",
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
