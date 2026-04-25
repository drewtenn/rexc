# Rexc Core Primitive Types Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Rust-like primitive core types to Rexc: signed and unsigned integers, booleans, characters, and strings.

**Architecture:** Introduce a shared primitive type model, then thread it through parser, AST, semantic analysis, IR, x86 codegen, and CLI diagnostics. The current i386 backend will support 32-bit-and-smaller scalar values and string literal addresses, while reporting a diagnostic for `i64` and `u64` code generation.

**Tech Stack:** C++17, CMake, current hand-written parser matching `grammar/Rexc.g4`, custom test runner, GNU i386 assembly syntax.

---

## File Structure

- Create `include/rexc/types.hpp` and `src/types.cpp`: primitive type representation, parsing, formatting, range bounds, and backend support helpers.
- Modify `include/rexc/ast.hpp` and `src/ast.cpp`: add bool, char, string, and unary expressions; widen integer literal storage.
- Modify `src/parse.cpp` and `grammar/Rexc.g4`: parse all primitive type names plus bool, char, string, and unary minus literals.
- Modify `include/rexc/sema.hpp` and `src/sema.cpp`: replace string-based type checking with primitive type checking and local type maps.
- Modify `include/rexc/ir.hpp`, `src/ir.cpp`, `include/rexc/lower_ir.hpp`, and `src/lower_ir.cpp`: carry resolved primitive types and new literal values into IR.
- Modify `include/rexc/codegen_x86.hpp` and `src/codegen_x86.cpp`: add diagnostic-aware codegen, string `.rodata`, bool/char/scalar emission, unsigned division, and i64/u64 rejection.
- Modify `src/main.cpp`: print codegen diagnostics and fail cleanly.
- Modify tests: `tests/frontend_tests.cpp`, `tests/sema_tests.cpp`, `tests/ir_tests.cpp`, `tests/codegen_tests.cpp`, and `tests/cli_smoke.sh`.
- Modify `examples/add.rx` only if needed; add `examples/types.rx` for mixed primitive compilation.

## Task 1: Shared Primitive Type Model

**Files:**
- Create: `include/rexc/types.hpp`
- Create: `src/types.cpp`
- Create: `tests/types_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing primitive type tests**

Add `tests/types_tests.cpp`:

```cpp
#include "rexc/types.hpp"
#include "test_support.hpp"

#include <string>

TEST_CASE(types_parse_all_core_primitive_names)
{
	const char *names[] = {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "bool", "char", "str"};
	for (const char *name : names) {
		auto type = rexc::parse_primitive_type(name);
		REQUIRE(type.has_value());
		REQUIRE_EQ(rexc::format_type(*type), std::string(name));
	}
}

TEST_CASE(types_report_integer_properties)
{
	auto i16 = *rexc::parse_primitive_type("i16");
	auto u32 = *rexc::parse_primitive_type("u32");
	auto str = *rexc::parse_primitive_type("str");

	REQUIRE(rexc::is_integer(i16));
	REQUIRE(rexc::is_signed_integer(i16));
	REQUIRE(rexc::is_integer(u32));
	REQUIRE(!rexc::is_signed_integer(u32));
	REQUIRE(!rexc::is_integer(str));
	REQUIRE(rexc::is_i386_codegen_supported(i16));
	REQUIRE(rexc::is_i386_codegen_supported(u32));
	REQUIRE(!rexc::is_i386_codegen_supported(*rexc::parse_primitive_type("i64")));
	REQUIRE(!rexc::is_i386_codegen_supported(*rexc::parse_primitive_type("u64")));
}

TEST_CASE(types_check_integer_literal_ranges)
{
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i8"), -128));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("i8"), 127));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("i8"), 128));
	REQUIRE(rexc::integer_literal_fits(*rexc::parse_primitive_type("u8"), 255));
	REQUIRE(!rexc::integer_literal_fits(*rexc::parse_primitive_type("u8"), -1));
}
```

Update `CMakeLists.txt` so `rexc_core` includes `src/types.cpp` and `rexc_tests` includes `tests/types_tests.cpp`.

- [ ] **Step 2: Run the failing test**

Run:

```sh
cmake --build build
```

Expected: FAIL because `rexc/types.hpp` does not exist.

- [ ] **Step 3: Implement the primitive type API**

Create `include/rexc/types.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rexc {

enum class PrimitiveKind {
	SignedInteger,
	UnsignedInteger,
	Bool,
	Char,
	Str,
};

struct PrimitiveType {
	PrimitiveKind kind;
	int bits = 0;

	friend bool operator==(PrimitiveType lhs, PrimitiveType rhs)
	{
		return lhs.kind == rhs.kind && lhs.bits == rhs.bits;
	}

	friend bool operator!=(PrimitiveType lhs, PrimitiveType rhs)
	{
		return !(lhs == rhs);
	}
};

std::optional<PrimitiveType> parse_primitive_type(const std::string &name);
std::string format_type(PrimitiveType type);
bool is_integer(PrimitiveType type);
bool is_signed_integer(PrimitiveType type);
bool is_unsigned_integer(PrimitiveType type);
bool is_i386_codegen_supported(PrimitiveType type);
bool integer_literal_fits(PrimitiveType type, std::int64_t value);

} // namespace rexc
```

Create `src/types.cpp` with exact mappings:

```cpp
#include "rexc/types.hpp"

#include <limits>

namespace rexc {

std::optional<PrimitiveType> parse_primitive_type(const std::string &name)
{
	if (name == "i8") return PrimitiveType{PrimitiveKind::SignedInteger, 8};
	if (name == "i16") return PrimitiveType{PrimitiveKind::SignedInteger, 16};
	if (name == "i32") return PrimitiveType{PrimitiveKind::SignedInteger, 32};
	if (name == "i64") return PrimitiveType{PrimitiveKind::SignedInteger, 64};
	if (name == "u8") return PrimitiveType{PrimitiveKind::UnsignedInteger, 8};
	if (name == "u16") return PrimitiveType{PrimitiveKind::UnsignedInteger, 16};
	if (name == "u32") return PrimitiveType{PrimitiveKind::UnsignedInteger, 32};
	if (name == "u64") return PrimitiveType{PrimitiveKind::UnsignedInteger, 64};
	if (name == "bool") return PrimitiveType{PrimitiveKind::Bool, 1};
	if (name == "char") return PrimitiveType{PrimitiveKind::Char, 32};
	if (name == "str") return PrimitiveType{PrimitiveKind::Str, 32};
	return std::nullopt;
}

std::string format_type(PrimitiveType type)
{
	switch (type.kind) {
	case PrimitiveKind::SignedInteger: return "i" + std::to_string(type.bits);
	case PrimitiveKind::UnsignedInteger: return "u" + std::to_string(type.bits);
	case PrimitiveKind::Bool: return "bool";
	case PrimitiveKind::Char: return "char";
	case PrimitiveKind::Str: return "str";
	}
	return "<invalid>";
}

bool is_integer(PrimitiveType type)
{
	return type.kind == PrimitiveKind::SignedInteger || type.kind == PrimitiveKind::UnsignedInteger;
}

bool is_signed_integer(PrimitiveType type)
{
	return type.kind == PrimitiveKind::SignedInteger;
}

bool is_unsigned_integer(PrimitiveType type)
{
	return type.kind == PrimitiveKind::UnsignedInteger;
}

bool is_i386_codegen_supported(PrimitiveType type)
{
	return type.kind != PrimitiveKind::SignedInteger && type.kind != PrimitiveKind::UnsignedInteger ? true : type.bits <= 32;
}

bool integer_literal_fits(PrimitiveType type, std::int64_t value)
{
	if (type.kind == PrimitiveKind::SignedInteger) {
		if (type.bits == 64)
			return true;
		std::int64_t min = -(std::int64_t{1} << (type.bits - 1));
		std::int64_t max = (std::int64_t{1} << (type.bits - 1)) - 1;
		return value >= min && value <= max;
	}
	if (type.kind == PrimitiveKind::UnsignedInteger) {
		if (value < 0)
			return false;
		if (type.bits == 64)
			return true;
		std::uint64_t max = (std::uint64_t{1} << type.bits) - 1;
		return static_cast<std::uint64_t>(value) <= max;
	}
	return false;
}

} // namespace rexc
```

- [ ] **Step 4: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```sh
git add CMakeLists.txt include/rexc/types.hpp src/types.cpp tests/types_tests.cpp
git commit -m "feat: add primitive type model"
```

## Task 2: Parser And AST Literal Support

**Files:**
- Modify: `grammar/Rexc.g4`
- Modify: `include/rexc/ast.hpp`
- Modify: `src/ast.cpp`
- Modify: `src/parse.cpp`
- Modify: `tests/frontend_tests.cpp`

- [ ] **Step 1: Write failing parser tests**

Append tests to `tests/frontend_tests.cpp`:

```cpp
TEST_CASE(parser_accepts_all_core_type_names)
{
	rexc::SourceFile source("test.rx",
		"fn f(a: i8, b: i16, c: i32, d: i64, e: u8, f: u16, g: u32, h: u64, i: bool, j: char, k: str) -> i32 { return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto result = rexc::parse_source(source, diagnostics);
	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().functions[0].parameters.size(), 11u);
	REQUIRE_EQ(result.module().functions[0].parameters[0].type.name, std::string("i8"));
	REQUIRE_EQ(result.module().functions[0].parameters[10].type.name, std::string("str"));
}

TEST_CASE(parser_accepts_bool_char_string_and_unary_literals)
{
	rexc::SourceFile source("test.rx",
		"fn main() -> i32 { let a: i32 = -12; let b: bool = true; let c: char = '\\n'; let d: str = \"hi\"; return a; }\n");
	rexc::Diagnostics diagnostics;
	auto result = rexc::parse_source(source, diagnostics);
	REQUIRE(result.ok());
	REQUIRE_EQ(result.module().functions[0].body.size(), 5u);
}
```

- [ ] **Step 2: Run the failing parser test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: FAIL because only `i32` and integer/name/call/binary expressions parse today.

- [ ] **Step 3: Extend AST expression kinds**

In `include/rexc/ast.hpp`, change `Expr::Kind` to include:

```cpp
enum class Kind { Integer, Bool, Char, String, Name, Unary, Binary, Call };
```

Change `IntegerExpr::value` to `std::int64_t`, add `<cstdint>`, and add:

```cpp
struct BoolExpr final : Expr {
	BoolExpr(SourceLocation location, bool value);
	bool value;
};

struct CharExpr final : Expr {
	CharExpr(SourceLocation location, char32_t value);
	char32_t value;
};

struct StringExpr final : Expr {
	StringExpr(SourceLocation location, std::string value);
	std::string value;
};

struct UnaryExpr final : Expr {
	UnaryExpr(SourceLocation location, std::string op, std::unique_ptr<Expr> operand);
	std::string op;
	std::unique_ptr<Expr> operand;
};
```

Implement these constructors in `src/ast.cpp`.

- [ ] **Step 4: Extend grammar and parser**

Update `grammar/Rexc.g4`:

```antlr
type
	: IDENT
	;

unary
	: '-' unary
	| primary
	;

multiplicative
	: unary (('*' | '/') unary)*
	;

primary
	: INTEGER
	| BOOL
	| CHAR
	| STRING
	| callExpression
	| IDENT
	| '(' expression ')'
	;

BOOL
	: 'true'
	| 'false'
	;

CHAR
	: '\'' (ESCAPE | ~['\\\r\n]) '\''
	;

STRING
	: '"' (ESCAPE | ~["\\\r\n])* '"'
	;

fragment ESCAPE
	: '\\' [nrt'"\\]
	;
```

In `src/parse.cpp`:

- replace the hard-coded `I32` token with type names parsed from identifiers and primitive keywords;
- add token kinds for `True`, `False`, `Char`, and `String`;
- add `character()` and `string()` lexing helpers;
- add escape decoding for `\n`, `\r`, `\t`, `\'`, `\"`, and `\\`;
- add `parse_unary()` between multiplicative and primary;
- create `ast::BoolExpr`, `ast::CharExpr`, `ast::StringExpr`, and `ast::UnaryExpr` in `parse_primary()` / `parse_unary()`.

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```sh
git add grammar/Rexc.g4 include/rexc/ast.hpp src/ast.cpp src/parse.cpp tests/frontend_tests.cpp
git commit -m "feat: parse core primitive literals"
```

## Task 3: Semantic Type Checking

**Files:**
- Modify: `src/sema.cpp`
- Modify: `tests/sema_tests.cpp`

- [ ] **Step 1: Write failing semantic tests**

Append tests to `tests/sema_tests.cpp`:

```cpp
TEST_CASE(sema_accepts_core_primitive_literals)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { let a: i8 = -1; let b: u8 = 255; let c: bool = false; let d: char = 'x'; let e: str = \"ok\"; return a; }\n",
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
```

- [ ] **Step 2: Run the failing semantic tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: FAIL because semantic analysis currently returns `"i32"` for every expression and tracks locals without types.

- [ ] **Step 3: Implement semantic type checking**

In `src/sema.cpp`:

- replace `std::unordered_set<std::string> locals` with `std::unordered_map<std::string, PrimitiveType> locals`;
- use `parse_primitive_type(type.name)` in `check_type()`;
- make `check_expr()` accept an optional expected type and return `std::optional<PrimitiveType>`;
- type integer literals using the expected type when one is available for local initializers, return expressions, and function call arguments;
- type integer literals as `i32` only when no expected type is available;
- for unary minus of an integer literal, evaluate the negative value and check against the expected type when one is available;
- return `bool`, `char`, and `str` for their literals;
- for names, return the local variable type;
- for calls, return the callee return type and compare each argument with the parameter type;
- for binary arithmetic, require both operands to be integer types and equal;
- compare local initializer type, call argument type, and return type exactly;
- emit the diagnostic strings used by the tests.

- [ ] **Step 4: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```sh
git add src/sema.cpp tests/sema_tests.cpp
git commit -m "feat: type check core primitives"
```

## Task 4: Typed IR For Core Primitives

**Files:**
- Modify: `include/rexc/ir.hpp`
- Modify: `src/ir.cpp`
- Modify: `src/lower_ir.cpp`
- Modify: `tests/ir_tests.cpp`

- [ ] **Step 1: Write failing IR tests**

Append to `tests/ir_tests.cpp`:

```cpp
TEST_CASE(lowering_preserves_non_i32_types_and_literals)
{
	rexc::SourceFile source("test.rx",
		"fn main() -> u32 { let ok: bool = true; let c: char = 'x'; let s: str = \"hi\"; let n: u32 = 42; return n; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());
	auto module = rexc::lower_to_ir(parsed.module());
	REQUIRE_EQ(rexc::format_type(module.functions[0].return_type), std::string("u32"));
	REQUIRE_EQ(module.functions[0].body.size(), 4u);
}
```

- [ ] **Step 2: Run the failing IR tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: FAIL because `ir::Type` is currently only `I32`.

- [ ] **Step 3: Replace IR type and values**

In `include/rexc/ir.hpp`:

- include `rexc/types.hpp`;
- replace `enum class Type { I32 };` with `using Type = PrimitiveType;`;
- change integer values to `std::int64_t`;
- add `BoolValue`, `CharValue`, `StringValue`, and `UnaryValue` classes;
- add value kinds `Bool`, `Char`, `String`, and `Unary`.

In `src/ir.cpp`, implement constructors for those values.

In `src/lower_ir.cpp`:

- map AST type names through `parse_primitive_type`;
- lower AST bool, char, string, and unary expressions into matching IR nodes;
- assign expression result types using semantic-compatible rules.

- [ ] **Step 4: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```sh
git add include/rexc/ir.hpp src/ir.cpp src/lower_ir.cpp tests/ir_tests.cpp
git commit -m "feat: lower core primitive IR"
```

## Task 5: i386 Codegen And Diagnostics

**Files:**
- Modify: `include/rexc/codegen_x86.hpp`
- Modify: `src/codegen_x86.cpp`
- Modify: `src/main.cpp`
- Modify: `tests/codegen_tests.cpp`

- [ ] **Step 1: Write failing codegen tests**

Append to `tests/codegen_tests.cpp`:

```cpp
TEST_CASE(codegen_emits_bool_char_and_string_literals)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { let ok: bool = true; let c: char = 'A'; let s: str = \"hi\"; return c; }\n");
	REQUIRE(assembly.find(".section .rodata") != std::string::npos);
	REQUIRE(assembly.find(".asciz \"hi\"") != std::string::npos);
	REQUIRE(assembly.find("movl $1, %eax") != std::string::npos);
	REQUIRE(assembly.find("movl $65, %eax") != std::string::npos);
	REQUIRE(assembly.find("movl $.Lstr") != std::string::npos);
}

TEST_CASE(codegen_reports_i64_as_unsupported_on_i386)
{
	rexc::SourceFile source("test.rx", "fn main() -> i64 { let x: i64 = 1; return x; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());
	auto module = rexc::lower_to_ir(parsed.module());
	auto result = rexc::emit_x86_assembly(module, diagnostics);
	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("64-bit integer code generation is not implemented for i386") != std::string::npos);
}
```

Update the existing helper in `tests/codegen_tests.cpp` to call `emit_x86_assembly(module, diagnostics)` and return `result.assembly()`.

- [ ] **Step 2: Run the failing codegen tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: FAIL because codegen has no diagnostic result and no rodata/string support.

- [ ] **Step 3: Add diagnostic-aware codegen result**

In `include/rexc/codegen_x86.hpp`, replace the old function declaration with:

```cpp
class CodegenResult {
public:
	CodegenResult(bool ok, std::string assembly);
	bool ok() const;
	const std::string &assembly() const;

private:
	bool ok_;
	std::string assembly_;
};

CodegenResult emit_x86_assembly(const ir::Module &module, Diagnostics &diagnostics);
```

Include `rexc/diagnostics.hpp`.

In `src/codegen_x86.cpp`:

- emit `.section .rodata` before `.text` when string literals are present;
- assign labels `.Lstr0`, `.Lstr1`, etc.;
- escape string payloads for `.asciz`;
- emit bool as `0` or `1`;
- emit char as its scalar value;
- emit string as `movl $.LstrN, %eax`;
- use `divl` for unsigned division and `idivl` for signed division;
- before emitting each function body, reject any return, parameter, local, or value type with `!is_i386_codegen_supported(type)` and add the required diagnostic;
- keep 8/16/32-bit values in 32-bit stack slots.

In `src/main.cpp`, call `emit_x86_assembly(ir, diagnostics)`, print diagnostics on failure, and write `result.assembly()` on success.

- [ ] **Step 4: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```sh
git add include/rexc/codegen_x86.hpp src/codegen_x86.cpp src/main.cpp tests/codegen_tests.cpp
git commit -m "feat: emit core primitive codegen"
```

## Task 6: CLI Example And Documentation

**Files:**
- Create: `examples/types.rx`
- Modify: `tests/cli_smoke.sh`
- Modify: `README.md`

- [ ] **Step 1: Write failing CLI smoke coverage**

Create `examples/types.rx`:

```rust
fn main() -> i32 {
    let signed: i16 = -12;
    let unsigned: u32 = 42;
    let enabled: bool = true;
    let letter: char = 'x';
    let message: str = "hello";
    return signed;
}
```

Append to `tests/cli_smoke.sh`:

```sh
"${build_dir}/rexc" "${repo_dir}/examples/types.rx" -S -o "${tmp_dir}/types.s"
test -s "${tmp_dir}/types.s"
grep -q ".asciz \"hello\"" "${tmp_dir}/types.s"
grep -q "movl $1, %eax" "${tmp_dir}/types.s"
```

- [ ] **Step 2: Run the failing CLI smoke test**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: FAIL until parser, sema, IR, and codegen are complete.

- [ ] **Step 3: Update README type examples**

Add a short section to `README.md`:

````markdown
## Core Primitive Types

Rexc supports Rust-like primitive type names:

```rust
i8 i16 i32 i64
u8 u16 u32 u64
bool
char
str
```

The i386 backend currently emits code for `i8`, `i16`, `i32`, `u8`, `u16`,
`u32`, `bool`, `char`, and `str`. It parses and type-checks `i64` and `u64`,
but reports a backend diagnostic if a function body needs 64-bit code
generation.
````

- [ ] **Step 4: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```sh
git add README.md examples/types.rx tests/cli_smoke.sh
git commit -m "docs: document core primitive types"
```

## Task 7: Final Verification

**Files:**
- Modify only if verification exposes a bug.

- [ ] **Step 1: Run clean configure**

Run:

```sh
rm -rf build
cmake -S . -B build
```

Expected: configure succeeds.

- [ ] **Step 2: Run clean build**

Run:

```sh
cmake --build build
```

Expected: build succeeds with `rexc` and `rexc_tests`.

- [ ] **Step 3: Run all tests**

Run:

```sh
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Compile primitive example**

Run:

```sh
build/rexc examples/types.rx -S -o build/types.s
sed -n '1,180p' build/types.s
```

Expected: assembly includes `.section .rodata`, `.asciz "hello"`, `movl $1, %eax`, `movl $120, %eax`, `main:`, and `ret`.

- [ ] **Step 5: Confirm 64-bit backend diagnostic**

Run:

```sh
printf 'fn main() -> i64 { let x: i64 = 1; return x; }\n' > build/i64.rx
! build/rexc build/i64.rx -S -o build/i64.s
```

Expected: command fails and stderr includes `64-bit integer code generation is not implemented for i386`.

- [ ] **Step 6: Check git status**

Run:

```sh
git status --short
```

Expected: clean worktree after final commit, or only unrelated pre-existing changes documented in the handoff.
