# Control Flow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add integer comparisons plus `if` and `if/else` statements to Rexc, with i386 and x86_64 assembly output.

**Architecture:** Extend the existing hand-written parser, AST, semantic analyzer, typed IR, and x86 backend. Comparisons produce `bool`; `if` statements carry a condition plus then/else statement bodies and lower to branch IR emitted with `cmp`, conditional jumps, and generated labels.

**Tech Stack:** C++17, CMake, current Rexc test harness, GNU assembler syntax for i386 and x86_64.

---

### Task 1: Parser and AST Shape

**Files:**
- Modify: `include/rexc/ast.hpp`
- Modify: `src/ast.cpp`
- Modify: `src/parse.cpp`
- Test: `tests/frontend_tests.cpp`

- [ ] **Step 1: Write failing frontend tests**

Add tests that parse comparison expressions and nested `if`/`else` statement bodies:

```cpp
TEST_CASE(parser_accepts_comparisons)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { return 1 <= 2; }\n");
	rexc::Diagnostics diagnostics;
	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &ret = static_cast<const rexc::ast::ReturnStmt &>(*result.module().functions[0].body[0]);
	REQUIRE_EQ(ret.value->kind, rexc::ast::Expr::Kind::Binary);
	const auto &binary = static_cast<const rexc::ast::BinaryExpr &>(*ret.value);
	REQUIRE_EQ(binary.op, std::string("<="));
}

TEST_CASE(parser_accepts_if_else_statements)
{
	rexc::SourceFile source("test.rx",
		"fn main() -> i32 { if 1 < 2 { return 1; } else { return 0; } }\n");
	rexc::Diagnostics diagnostics;
	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &stmt = *result.module().functions[0].body[0];
	REQUIRE_EQ(stmt.kind, rexc::ast::Stmt::Kind::If);
	const auto &if_stmt = static_cast<const rexc::ast::IfStmt &>(stmt);
	REQUIRE_EQ(if_stmt.condition->kind, rexc::ast::Expr::Kind::Binary);
	REQUIRE_EQ(if_stmt.then_body.size(), std::size_t(1));
	REQUIRE_EQ(if_stmt.else_body.size(), std::size_t(1));
}
```

- [ ] **Step 2: Verify red**

Run: `cmake --build build && build/rexc_tests`

Expected: compilation fails because `Stmt::Kind::If` and `ast::IfStmt` do not exist, or parser tests fail because `if` is not parsed.

- [ ] **Step 3: Implement AST and parser**

Add `IfStmt`, token kinds for comparison operators and `if`/`else`, parse comparison precedence below additive expressions, and parse `if` statements inside function bodies.

- [ ] **Step 4: Verify green**

Run: `cmake --build build && build/rexc_tests`

Expected: all unit tests pass.

### Task 2: Semantic Analysis and IR

**Files:**
- Modify: `include/rexc/ir.hpp`
- Modify: `src/ir.cpp`
- Modify: `src/sema.cpp`
- Modify: `src/lower_ir.cpp`
- Test: `tests/sema_tests.cpp`
- Test: `tests/ir_tests.cpp`

- [ ] **Step 1: Write failing semantic and IR tests**

Add tests that require comparisons to return `bool`, `if` conditions to be `bool`, and `if` statements to lower to IR:

```cpp
TEST_CASE(sema_accepts_if_else_with_bool_condition)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { if 1 < 2 { return 1; } else { return 0; } }\n", diagnostics);
	REQUIRE(result.ok());
}

TEST_CASE(sema_rejects_non_bool_if_condition)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { if 1 { return 1; } return 0; }\n", diagnostics);
	REQUIRE_FALSE(result.ok());
	REQUIRE(diagnostics.format().find("if condition must be bool") != std::string::npos);
}

TEST_CASE(ir_lowers_if_else_statement)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { if 1 < 2 { return 1; } else { return 0; } }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	auto sema = rexc::analyze_module(parsed.module(), diagnostics);
	REQUIRE(sema.ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &stmt = *module.functions[0].body[0];
	REQUIRE_EQ(stmt.kind, rexc::ir::Statement::Kind::If);
	const auto &if_stmt = static_cast<const rexc::ir::IfStatement &>(stmt);
	REQUIRE_EQ(if_stmt.condition->type, rexc::PrimitiveType{rexc::PrimitiveKind::Bool});
	REQUIRE_EQ(if_stmt.then_body.size(), std::size_t(1));
	REQUIRE_EQ(if_stmt.else_body.size(), std::size_t(1));
}
```

- [ ] **Step 2: Verify red**

Run: `cmake --build build && build/rexc_tests`

Expected: compilation fails because IR `IfStatement` does not exist or semantic checks do not handle it.

- [ ] **Step 3: Implement semantic and IR lowering**

Teach sema to recurse into both `if` branches with the same local environment, require `bool` conditions, and make comparison operators require same-typed integer operands and return `bool`. Add IR `IfStatement` and lower AST `IfStmt` into it.

- [ ] **Step 4: Verify green**

Run: `cmake --build build && build/rexc_tests`

Expected: all unit tests pass.

### Task 3: x86 Branch Codegen and CLI Smoke

**Files:**
- Modify: `src/codegen_x86.cpp`
- Modify: `tests/codegen_tests.cpp`
- Modify: `tests/cli_smoke.sh`
- Create: `examples/branch.rx`

- [ ] **Step 1: Write failing codegen tests**

Add tests requiring i386 and x86_64 branch assembly:

```cpp
TEST_CASE(codegen_i386_emits_if_else_comparison_branch)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { if 1 < 2 { return 7; } else { return 9; } }\n");

	REQUIRE(assembly.find("cmpl %ecx, %eax") != std::string::npos);
	REQUIRE(assembly.find("setl %al") != std::string::npos);
	REQUIRE(assembly.find("cmpb $0, %al") != std::string::npos);
	REQUIRE(assembly.find("je .L_else_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_return_main") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_if_else_comparison_branch)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i64 { if 1 <= 2 { return 7; } else { return 9; } }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("cmpq %rcx, %rax") != std::string::npos);
	REQUIRE(assembly.find("setle %al") != std::string::npos);
	REQUIRE(assembly.find("cmpb $0, %al") != std::string::npos);
	REQUIRE(assembly.find("je .L_else_") != std::string::npos);
	REQUIRE(assembly.find("jmp .L_return_main") != std::string::npos);
}
```

- [ ] **Step 2: Verify red**

Run: `cmake --build build && build/rexc_tests`

Expected: compilation fails or tests fail because branch codegen is not implemented.

- [ ] **Step 3: Implement branch codegen**

Emit comparison values as `0`/`1` in `%eax`/`%rax` using `cmp`, `setcc`, and zero-extension. Emit `if` statements with deterministic labels, `cmpb $0, %al`, `je`, unconditional branch over the else block, and recursive statement emission.

- [ ] **Step 4: Add CLI smoke example**

Create `examples/branch.rx` with an `if/else` program and update `tests/cli_smoke.sh` to compile it for both targets and check for branch instructions.

- [ ] **Step 5: Verify green**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`

Expected: all tests pass.

### Task 4: Documentation and Final Verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update README**

Document comparison operators and `if/else` in the language feature list and compiler pipeline.

- [ ] **Step 2: Final verification**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

Expected: build succeeds, all tests pass, and diff check reports no whitespace errors.
