# Rexc Layered Standard Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the first layered Rexc standard library milestone: prelude console I/O functions backed by automatically linked hosted runtime objects.

**Architecture:** Add call statements to the AST and IR, seed semantic analysis and lowering with compiler-known `std` prelude declarations, then link a generated per-target runtime object for executable builds. Keep `core`/`std` represented by a small compiler metadata module now, while preserving the current parser and backend shapes.

**Tech Stack:** C++17, ANTLR grammar generation, existing Rexc AST/sema/IR/codegen pipeline, GNU assembler syntax for i386/x86_64 ELF, Darwin ARM64 assembly, CMake test harness, POSIX shell smoke tests.

---

### Task 1: Call Statement Syntax

**Files:**
- Modify: `grammar/Rexc.g4`
- Modify: `include/rexc/ast.hpp`
- Modify: `src/ast.cpp`
- Modify: `src/parse.cpp`
- Test: `tests/frontend_tests.cpp`

- [ ] **Step 1: Write failing parser tests**

Append these tests to `tests/frontend_tests.cpp`:

```cpp
TEST_CASE(parser_accepts_call_statement)
{
	rexc::SourceFile source("test.rx",
		"fn main() -> i32 { println(\"hello\"); return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(result.ok());
	const auto &body = result.module().functions[0].body;
	REQUIRE_EQ(body.size(), std::size_t(2));
	REQUIRE_EQ(body[0]->kind, rexc::ast::Stmt::Kind::Expr);
	const auto &stmt = static_cast<const rexc::ast::ExprStmt &>(*body[0]);
	REQUIRE_EQ(stmt.value->kind, rexc::ast::Expr::Kind::Call);
	const auto &call = static_cast<const rexc::ast::CallExpr &>(*stmt.value);
	REQUIRE_EQ(call.callee, std::string("println"));
}

TEST_CASE(parser_rejects_non_call_expression_statement)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { 1 + 2; return 0; }\n");
	rexc::Diagnostics diagnostics;

	auto result = rexc::parse_source(source, diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.has_errors());
}
```

- [ ] **Step 2: Run tests to verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: compilation fails because `rexc::ast::Stmt::Kind::Expr` and `rexc::ast::ExprStmt` do not exist, or the parser rejects `println("hello");`.

- [ ] **Step 3: Extend grammar with call statements**

In `grammar/Rexc.g4`, add `callStatement` to `statement` before `returnStatement`:

```antlr
statement
	: letStatement
	| assignStatement
	| indirectAssignStatement
	| callStatement
	| returnStatement
	| ifStatement
	| whileStatement
	| breakStatement
	| continueStatement
	;
```

Add this rule after `indirectAssignStatement`:

```antlr
callStatement
	: callExpression ';'
	;
```

- [ ] **Step 4: Add AST node**

In `include/rexc/ast.hpp`, change the statement kind enum to include `Expr`:

```cpp
struct Stmt {
	enum class Kind { Let, Assign, IndirectAssign, Expr, Return, If, While, Break, Continue };
```

Add this struct after `IndirectAssignStmt`:

```cpp
struct ExprStmt final : Stmt {
	ExprStmt(SourceLocation location, std::unique_ptr<Expr> value);

	std::unique_ptr<Expr> value;
};
```

In `src/ast.cpp`, add:

```cpp
ExprStmt::ExprStmt(SourceLocation location, std::unique_ptr<Expr> value)
	: Stmt(Kind::Expr, std::move(location)), value(std::move(value))
{
}
```

- [ ] **Step 5: Build AST call statements**

In `src/parse.cpp`, update `build_statement` so `callStatement()` maps to an `ExprStmt`:

```cpp
if (auto *call_statement = context->callStatement())
	return build_call_statement(call_statement);
```

Add this method near the other statement builders:

```cpp
std::unique_ptr<ast::Stmt> build_call_statement(RexcParser::CallStatementContext *context)
{
	return std::make_unique<ast::ExprStmt>(
		location(context), build_call_expression(context->callExpression()));
}
```

- [ ] **Step 6: Verify parser green**

Run:

```sh
cmake -S . -B build
cmake --build build
build/rexc_tests
```

Expected: parser tests pass; later sema or lowering tests may not cover call statements yet.

- [ ] **Step 7: Commit**

Run:

```sh
git add grammar/Rexc.g4 include/rexc/ast.hpp src/ast.cpp src/parse.cpp tests/frontend_tests.cpp
git commit -m "feat: parse call statements"
```

### Task 2: Prelude Declarations In Sema

**Files:**
- Create: `include/rexc/stdlib.hpp`
- Create: `src/stdlib.cpp`
- Modify: `src/sema.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/sema_tests.cpp`

- [ ] **Step 1: Write failing semantic tests**

Append these tests to `tests/sema_tests.cpp`:

```cpp
TEST_CASE(sema_accepts_std_prelude_print_functions)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { print(\"hello\"); println(\"world\"); return 0; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_accepts_std_prelude_read_line)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 { let name: str = read_line(); println(name); return 0; }\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_std_prelude_wrong_argument_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { println(1); return 0; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("argument type mismatch: expected 'str' but got 'i32'") != std::string::npos);
}

TEST_CASE(sema_rejects_duplicate_std_prelude_function)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn println(value: str) -> i32 { return 0; }\n"
		"fn main() -> i32 { return 0; }\n",
		diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate function 'println'") != std::string::npos);
}
```

- [ ] **Step 2: Run tests to verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: compilation fails until `ExprStmt` is handled in sema, or sema rejects `print`, `println`, and `read_line` as unknown functions.

- [ ] **Step 3: Add standard-library metadata API**

Create `include/rexc/stdlib.hpp`:

```cpp
#pragma once

#include "rexc/codegen.hpp"
#include "rexc/types.hpp"

#include <string>
#include <vector>

namespace rexc::stdlib {

struct FunctionDecl {
	std::string name;
	std::vector<PrimitiveType> parameters;
	PrimitiveType return_type;
};

const std::vector<FunctionDecl> &prelude_functions();
const FunctionDecl *find_prelude_function(const std::string &name);
std::string hosted_runtime_assembly(CodegenTarget target);

} // namespace rexc::stdlib
```

Create `src/stdlib.cpp` with declarations first; runtime assembly returns an empty string until Task 5:

```cpp
#include "rexc/stdlib.hpp"

#include <string>

namespace rexc::stdlib {
namespace {

PrimitiveType i32_type()
{
	return PrimitiveType{PrimitiveKind::SignedInteger, 32};
}

PrimitiveType str_type()
{
	return PrimitiveType{PrimitiveKind::Str};
}

} // namespace

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions{
		FunctionDecl{"print", {str_type()}, i32_type()},
		FunctionDecl{"println", {str_type()}, i32_type()},
		FunctionDecl{"read_line", {}, str_type()},
		FunctionDecl{"exit", {i32_type()}, i32_type()},
	};
	return functions;
}

const FunctionDecl *find_prelude_function(const std::string &name)
{
	for (const auto &function : prelude_functions()) {
		if (function.name == name)
			return &function;
	}
	return nullptr;
}

std::string hosted_runtime_assembly(CodegenTarget)
{
	return "";
}

} // namespace rexc::stdlib
```

- [ ] **Step 4: Add stdlib source to CMake**

In `CMakeLists.txt`, add `src/stdlib.cpp` to `add_library(rexc_core ...)` after `src/types.cpp`:

```cmake
	src/types.cpp
	src/stdlib.cpp
```

- [ ] **Step 5: Seed sema with prelude functions**

In `src/sema.cpp`, include the metadata:

```cpp
#include "rexc/stdlib.hpp"
```

Add the vector header near the other standard-library includes:

```cpp
#include <vector>
```

Replace `FunctionInfo` with:

```cpp
struct FunctionInfo {
	SourceLocation location;
	PrimitiveType return_type = PrimitiveType{PrimitiveKind::SignedInteger, 32};
	std::vector<PrimitiveType> parameter_types;
};
```

At the beginning of `build_function_table()`, seed the map:

```cpp
for (const auto &function : stdlib::prelude_functions())
	functions_[function.name] =
		FunctionInfo{SourceLocation{}, function.return_type, function.parameters};
```

Then store user functions with resolved signatures:

```cpp
for (const auto &function : module_.functions) {
	if (functions_.find(function.name) != functions_.end()) {
		diagnostics_.error(function.location, "duplicate function '" + function.name + "'");
		continue;
	}
	FunctionInfo info;
	info.location = function.location;
	info.return_type = check_type(function.return_type);
	for (const auto &parameter : function.parameters)
		info.parameter_types.push_back(check_type(parameter.type));
	functions_[function.name] = std::move(info);
}
```

In `check_expr` for `ast::Expr::Kind::Call`, replace accesses to `it->second.function->parameters` and `it->second.function->return_type` with:

```cpp
std::size_t expected_count = it->second.parameter_types.size();
if (expected_count != call.arguments.size()) {
	diagnostics_.error(call.location, "function '" + call.callee + "' expected " +
	                   std::to_string(expected_count) + " arguments but got " +
	                   std::to_string(call.arguments.size()));
}
for (std::size_t i = 0; i < call.arguments.size(); ++i) {
	std::optional<PrimitiveType> parameter_type;
	if (i < expected_count)
		parameter_type = it->second.parameter_types[i];
	auto argument_type = check_expr(locals, *call.arguments[i], parameter_type);
	if (parameter_type && argument_type && *argument_type != *parameter_type) {
		diagnostics_.error(call.arguments[i]->location,
		                   "argument type mismatch: expected '" +
		                   format_type(*parameter_type) + "' but got '" +
		                   format_type(*argument_type) + "'");
	}
}
return it->second.return_type;
```

- [ ] **Step 6: Handle expression statements in sema**

In `analyze_statement`, before `Return`, add:

```cpp
if (statement.kind == ast::Stmt::Kind::Expr) {
	const auto &expr_statement = static_cast<const ast::ExprStmt &>(statement);
	check_expr(locals, *expr_statement.value);
	return;
}
```

- [ ] **Step 7: Verify sema green**

Run:

```sh
cmake -S . -B build
cmake --build build
build/rexc_tests
```

Expected: semantic tests for prelude functions pass.

- [ ] **Step 8: Commit**

Run:

```sh
git add include/rexc/stdlib.hpp src/stdlib.cpp src/sema.cpp CMakeLists.txt tests/sema_tests.cpp
git commit -m "feat: add std prelude declarations"
```

### Task 3: Lower Call Statements To IR

**Files:**
- Modify: `include/rexc/ir.hpp`
- Modify: `src/ir.cpp`
- Modify: `src/lower_ir.cpp`
- Test: `tests/ir_tests.cpp`

- [ ] **Step 1: Write failing IR test**

Append this test to `tests/ir_tests.cpp`:

```cpp
TEST_CASE(lowering_lowers_call_statement)
{
	rexc::SourceFile source("test.rx", "fn main() -> i32 { println(\"hello\"); return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	auto sema = rexc::analyze_module(parsed.module(), diagnostics);
	REQUIRE(sema.ok());

	auto module = rexc::lower_to_ir(parsed.module());

	const auto &stmt = *module.functions[0].body[0];
	REQUIRE_EQ(stmt.kind, rexc::ir::Statement::Kind::Expr);
	const auto &expr_stmt = static_cast<const rexc::ir::ExprStatement &>(stmt);
	REQUIRE_EQ(expr_stmt.value->kind, rexc::ir::Value::Kind::Call);
	const auto &call = static_cast<const rexc::ir::CallValue &>(*expr_stmt.value);
	REQUIRE_EQ(call.callee, std::string("println"));
	REQUIRE_EQ(call.arguments.size(), std::size_t(1));
}
```

- [ ] **Step 2: Run tests to verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: compilation fails because `ir::Statement::Kind::Expr` and `ir::ExprStatement` do not exist.

- [ ] **Step 3: Add IR expression statement**

In `include/rexc/ir.hpp`, add `Expr` to the statement kind enum:

```cpp
struct Statement {
	enum class Kind { Let, Assign, IndirectAssign, Expr, Return, If, While, Break, Continue };
```

Add after `IndirectAssignStatement`:

```cpp
struct ExprStatement final : Statement {
	explicit ExprStatement(std::unique_ptr<Value> value);

	std::unique_ptr<Value> value;
};
```

In `src/ir.cpp`, add:

```cpp
ExprStatement::ExprStatement(std::unique_ptr<Value> value)
	: Statement(Kind::Expr), value(std::move(value))
{
}
```

- [ ] **Step 4: Seed lowering with prelude functions**

In `src/lower_ir.cpp`, include:

```cpp
#include "rexc/stdlib.hpp"
```

At the beginning of `build_function_table()`, add:

```cpp
for (const auto &function : stdlib::prelude_functions()) {
	FunctionInfo info;
	info.return_type = function.return_type;
	info.parameter_types = function.parameters;
	functions_[function.name] = std::move(info);
}
```

Keep the existing user-function loop after this seed.

- [ ] **Step 5: Lower AST expression statements**

In `lower_statement`, before `Return`, add:

```cpp
if (statement.kind == ast::Stmt::Kind::Expr) {
	const auto &expr_statement = static_cast<const ast::ExprStmt &>(statement);
	return std::make_unique<ir::ExprStatement>(
		lower_expr(*expr_statement.value, locals));
}
```

- [ ] **Step 6: Verify IR green**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: all unit tests pass.

- [ ] **Step 7: Commit**

Run:

```sh
git add include/rexc/ir.hpp src/ir.cpp src/lower_ir.cpp tests/ir_tests.cpp
git commit -m "feat: lower call statements"
```

### Task 4: Codegen For Call Statements

**Files:**
- Modify: `src/codegen_x86.cpp`
- Modify: `src/codegen_arm64.cpp`
- Test: `tests/codegen_tests.cpp`
- Test: `tests/codegen_arm64_tests.cpp`

- [ ] **Step 1: Write failing codegen tests**

Append to `tests/codegen_tests.cpp`:

```cpp
TEST_CASE(codegen_i386_emits_call_statement)
{
	auto assembly = compile_to_assembly("fn main() -> i32 { println(\"hello\"); return 0; }\n");

	REQUIRE(assembly.find("call println") != std::string::npos);
	REQUIRE(assembly.find("addl $4, %esp") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_call_statement)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i64 { println(\"hello\"); return 0; }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("call println") != std::string::npos);
	REQUIRE(assembly.find("popq %rdi") != std::string::npos);
}
```

Append to `tests/codegen_arm64_tests.cpp`:

```cpp
TEST_CASE(codegen_arm64_macos_emits_call_statement)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { println(\"hello\"); return 0; }\n");

	REQUIRE(assembly.find("bl _println") != std::string::npos);
}
```

- [ ] **Step 2: Run tests to verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: codegen validation or emission fails because `ir::Statement::Kind::Expr` is not handled.

- [ ] **Step 3: Handle expression statements in x86 backend**

In `src/codegen_x86.cpp`, update each statement traversal:

In `validate_statement`, before `If`, add:

```cpp
if (statement.kind == ir::Statement::Kind::Expr) {
	const auto &expr = static_cast<const ir::ExprStatement &>(statement);
	return validate_value(*expr.value);
}
```

In `emit_statement`, before `If`, add:

```cpp
if (statement.kind == ir::Statement::Kind::Expr) {
	const auto &expr = static_cast<const ir::ExprStatement &>(statement);
	emit_value(*expr.value, frame, slots);
	return;
}
```

In `collect_string_labels(const ir::Statement &)`, before `If`, add:

```cpp
if (statement.kind == ir::Statement::Kind::Expr) {
	const auto &expr = static_cast<const ir::ExprStatement &>(statement);
	collect_string_labels(*expr.value);
	return;
}
```

In `emit_string_literals(const ir::Statement &)`, before `If`, add:

```cpp
if (statement.kind == ir::Statement::Kind::Expr) {
	const auto &expr = static_cast<const ir::ExprStatement &>(statement);
	emit_string_literals(*expr.value);
	return;
}
```

- [ ] **Step 4: Handle expression statements in ARM64 backend**

In `src/codegen_arm64.cpp`, add the same shape to statement handling:

In `emit_statement`, before `If`, add:

```cpp
if (statement.kind == ir::Statement::Kind::Expr) {
	const auto &expr = static_cast<const ir::ExprStatement &>(statement);
	emit_value(*expr.value, frame, slots);
	return;
}
```

In `collect_string_labels(const ir::Statement &)`, before `If`, add:

```cpp
} else if (statement.kind == ir::Statement::Kind::Expr) {
	const auto &expr = static_cast<const ir::ExprStatement &>(statement);
	collect_string_labels(*expr.value);
} else if (statement.kind == ir::Statement::Kind::If) {
```

In `emit_string_literals(const ir::Statement &)`, before `If`, add:

```cpp
} else if (statement.kind == ir::Statement::Kind::Expr) {
	const auto &expr = static_cast<const ir::ExprStatement &>(statement);
	emit_string_literals(*expr.value);
} else if (statement.kind == ir::Statement::Kind::If) {
```

- [ ] **Step 5: Verify codegen green**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: all backend tests pass.

- [ ] **Step 6: Commit**

Run:

```sh
git add src/codegen_x86.cpp src/codegen_arm64.cpp tests/codegen_tests.cpp tests/codegen_arm64_tests.cpp
git commit -m "feat: emit call statements"
```

### Task 5: Hosted Runtime Assembly Metadata

**Files:**
- Modify: `src/stdlib.cpp`
- Test: `tests/stdlib_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing runtime metadata tests**

Create `tests/stdlib_tests.cpp`:

```cpp
#include "rexc/codegen.hpp"
#include "rexc/stdlib.hpp"
#include "test_support.hpp"

#include <string>

TEST_CASE(stdlib_declares_prelude_functions)
{
	auto print = rexc::stdlib::find_prelude_function("print");
	auto println = rexc::stdlib::find_prelude_function("println");
	auto read_line = rexc::stdlib::find_prelude_function("read_line");
	auto exit = rexc::stdlib::find_prelude_function("exit");

	REQUIRE(print != nullptr);
	REQUIRE(println != nullptr);
	REQUIRE(read_line != nullptr);
	REQUIRE(exit != nullptr);
	REQUIRE_EQ(print->parameters.size(), std::size_t(1));
	REQUIRE_EQ(read_line->parameters.size(), std::size_t(0));
}

TEST_CASE(stdlib_emits_hosted_runtime_symbols)
{
	auto i386 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::I386);
	auto x86_64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::X86_64);
	auto arm64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::ARM64_MACOS);

	REQUIRE(i386.find("print:") != std::string::npos);
	REQUIRE(i386.find("println:") != std::string::npos);
	REQUIRE(i386.find("read_line:") != std::string::npos);
	REQUIRE(i386.find("exit:") != std::string::npos);
	REQUIRE(x86_64.find("syscall") != std::string::npos);
	REQUIRE(arm64.find("_print:") != std::string::npos);
	REQUIRE(arm64.find("bl _write") != std::string::npos);
	REQUIRE(arm64.find("_read_line:") != std::string::npos);
}
```

Add `tests/stdlib_tests.cpp` to `rexc_tests` in `CMakeLists.txt`.

- [ ] **Step 2: Run tests to verify red**

Run:

```sh
cmake -S . -B build
cmake --build build
build/rexc_tests
```

Expected: `stdlib_emits_hosted_runtime_symbols` fails because `hosted_runtime_assembly` returns an empty string.

- [ ] **Step 3: Add ELF i386 runtime assembly**

In `src/stdlib.cpp`, replace `hosted_runtime_assembly` with target dispatch helpers. The i386 assembly string must contain these labels and syscall behavior:

```cpp
std::string i386_hosted_runtime_assembly()
{
	return R"asm(
```

```asm
.section .rodata
.Lrexc_newline:
	.byte 10
.section .bss
.Lrexc_read_line_buffer:
	.zero 1024
.text
.globl print
print:
	pushl %ebp
	movl %esp, %ebp
	movl 8(%ebp), %ecx
	xorl %edx, %edx
.Lrexc_i386_print_len:
	cmpb $0, (%ecx,%edx)
	je .Lrexc_i386_print_write
	incl %edx
	jmp .Lrexc_i386_print_len
.Lrexc_i386_print_write:
	movl $4, %eax
	movl $1, %ebx
	int $0x80
	leave
	ret
.globl println
println:
	pushl %ebp
	movl %esp, %ebp
	pushl 8(%ebp)
	call print
	addl $4, %esp
	movl %eax, %esi
	movl $4, %eax
	movl $1, %ebx
	movl $.Lrexc_newline, %ecx
	movl $1, %edx
	int $0x80
	addl %esi, %eax
	leave
	ret
.globl read_line
read_line:
	pushl %ebp
	movl %esp, %ebp
	movl $3, %eax
	movl $0, %ebx
	movl $.Lrexc_read_line_buffer, %ecx
	movl $1023, %edx
	int $0x80
	cmpl $0, %eax
	jle .Lrexc_i386_read_empty
	movl %eax, %edx
	decl %edx
	cmpb $10, .Lrexc_read_line_buffer(%edx)
	jne .Lrexc_i386_read_terminate
	movb $0, .Lrexc_read_line_buffer(%edx)
	jmp .Lrexc_i386_read_done
.Lrexc_i386_read_terminate:
	movb $0, .Lrexc_read_line_buffer(%eax)
	jmp .Lrexc_i386_read_done
.Lrexc_i386_read_empty:
	movb $0, .Lrexc_read_line_buffer
.Lrexc_i386_read_done:
	movl $.Lrexc_read_line_buffer, %eax
	leave
	ret
.globl exit
exit:
	movl 4(%esp), %ebx
	movl $1, %eax
	int $0x80
```

Close the helper after the assembly text:

```cpp
)asm";
}
```

- [ ] **Step 4: Add ELF x86_64 runtime assembly**

Add x86_64 assembly with these labels and syscall behavior:

```cpp
std::string x86_64_hosted_runtime_assembly()
{
	return R"asm(
```

```asm
.section .rodata
.Lrexc_newline:
	.byte 10
.section .bss
.Lrexc_read_line_buffer:
	.zero 1024
.text
.globl print
print:
	movq %rdi, %rsi
	xorq %rdx, %rdx
.Lrexc_x64_print_len:
	cmpb $0, (%rsi,%rdx)
	je .Lrexc_x64_print_write
	incq %rdx
	jmp .Lrexc_x64_print_len
.Lrexc_x64_print_write:
	movq $1, %rax
	movq $1, %rdi
	syscall
	ret
.globl println
println:
	pushq %rdi
	call print
	movq %rax, %r8
	movq $1, %rax
	movq $1, %rdi
	leaq .Lrexc_newline(%rip), %rsi
	movq $1, %rdx
	syscall
	addq %r8, %rax
	popq %rdi
	ret
.globl read_line
read_line:
	movq $0, %rax
	movq $0, %rdi
	leaq .Lrexc_read_line_buffer(%rip), %rsi
	movq $1023, %rdx
	syscall
	cmpq $0, %rax
	jle .Lrexc_x64_read_empty
	movq %rax, %rdx
	decq %rdx
	leaq .Lrexc_read_line_buffer(%rip), %rsi
	cmpb $10, (%rsi,%rdx)
	jne .Lrexc_x64_read_terminate
	movb $0, (%rsi,%rdx)
	jmp .Lrexc_x64_read_done
.Lrexc_x64_read_terminate:
	movb $0, (%rsi,%rax)
	jmp .Lrexc_x64_read_done
.Lrexc_x64_read_empty:
	leaq .Lrexc_read_line_buffer(%rip), %rsi
	movb $0, (%rsi)
.Lrexc_x64_read_done:
	leaq .Lrexc_read_line_buffer(%rip), %rax
	ret
.globl exit
exit:
	movq %rdi, %rdi
	movq $60, %rax
	syscall
```

Close the helper after the assembly text:

```cpp
)asm";
}
```

- [ ] **Step 5: Add Darwin ARM64 runtime assembly**

Add Darwin assembly with libc calls:

```cpp
std::string arm64_macos_hosted_runtime_assembly()
{
	return R"asm(
```

```asm
.cstring
Lrexc_newline:
	.byte 10
.zerofill __DATA,__bss,Lrexc_read_line_buffer,1024,4
.text
.globl _print
.p2align 2
_print:
	stp x29, x30, [sp, #-32]!
	mov x29, sp
	str x19, [sp, #16]
	mov x19, x0
	mov x1, x0
	mov x2, #0
Lrexc_arm64_print_len:
	ldrb w3, [x1, x2]
	cbz w3, Lrexc_arm64_print_write
	add x2, x2, #1
	b Lrexc_arm64_print_len
Lrexc_arm64_print_write:
	mov x0, #1
	mov x1, x19
	bl _write
	ldr x19, [sp, #16]
	ldp x29, x30, [sp], #32
	ret
.globl _println
.p2align 2
_println:
	stp x29, x30, [sp, #-32]!
	mov x29, sp
	str x19, [sp, #16]
	bl _print
	mov x19, x0
	mov x0, #1
	adrp x1, Lrexc_newline@PAGE
	add x1, x1, Lrexc_newline@PAGEOFF
	mov x2, #1
	bl _write
	add x0, x19, x0
	ldr x19, [sp, #16]
	ldp x29, x30, [sp], #32
	ret
.globl _read_line
.p2align 2
_read_line:
	stp x29, x30, [sp, #-48]!
	mov x29, sp
	str x19, [sp, #16]
	str x20, [sp, #24]
	mov x0, #0
	adrp x1, Lrexc_read_line_buffer@PAGE
	add x1, x1, Lrexc_read_line_buffer@PAGEOFF
	mov x19, x1
	mov x2, #1023
	bl _read
	cmp x0, #0
	b.le Lrexc_arm64_read_empty
	mov x20, x0
	sub x2, x20, #1
	ldrb w3, [x19, x2]
	cmp w3, #10
	b.ne Lrexc_arm64_read_terminate
	strb wzr, [x19, x2]
	b Lrexc_arm64_read_done
Lrexc_arm64_read_terminate:
	strb wzr, [x19, x20]
	b Lrexc_arm64_read_done
Lrexc_arm64_read_empty:
	strb wzr, [x19]
Lrexc_arm64_read_done:
	mov x0, x19
	ldr x20, [sp, #24]
	ldr x19, [sp, #16]
	ldp x29, x30, [sp], #48
	ret
```

Close the helper after the assembly text:

```cpp
)asm";
}
```

Do not define `_exit` in the Darwin runtime; source calls to `exit` resolve to libc's `_exit` symbol through the existing Darwin symbol prefixing.

After the helpers, add the public dispatch function:

```cpp
std::string hosted_runtime_assembly(CodegenTarget target)
{
	switch (target) {
	case CodegenTarget::I386:
		return i386_hosted_runtime_assembly();
	case CodegenTarget::X86_64:
		return x86_64_hosted_runtime_assembly();
	case CodegenTarget::ARM64_MACOS:
		return arm64_macos_hosted_runtime_assembly();
	}
	return "";
}
```

- [ ] **Step 6: Verify runtime metadata green**

Run:

```sh
cmake -S . -B build
cmake --build build
build/rexc_tests
```

Expected: stdlib tests pass and existing tests still pass.

- [ ] **Step 7: Commit**

Run:

```sh
git add src/stdlib.cpp tests/stdlib_tests.cpp CMakeLists.txt
git commit -m "feat: add hosted std runtime assembly"
```

### Task 6: Link Hosted Runtime For Executables

**Files:**
- Modify: `src/main.cpp`
- Test: `tests/assemble_smoke.sh`
- Create: `examples/std_io.rx`

- [ ] **Step 1: Create standard I/O example**

Create `examples/std_io.rx`:

```rust
fn main() -> i32 {
    println("hello from rexc");
    print("echo: ");
    let line: str = read_line();
    println(line);
    return 0;
}
```

- [ ] **Step 2: Add failing link smoke**

In `tests/assemble_smoke.sh`, inside the Darwin arm64 block, after the existing `add-arm64` run check, add:

```sh
"${build_dir}/rexc" "${repo_dir}/examples/std_io.rx" -o "${tmp_dir}/std-io-arm64"
test -x "${tmp_dir}/std-io-arm64"
printf 'friend\n' | "${tmp_dir}/std-io-arm64" > "${tmp_dir}/std-io-arm64.out"
grep -F -q 'hello from rexc' "${tmp_dir}/std-io-arm64.out"
grep -F -q 'echo: friend' "${tmp_dir}/std-io-arm64.out"
```

Inside the Darwin x86 ELF block, after `file` checks, add:

```sh
"${build_dir}/rexc" "${repo_dir}/examples/std_io.rx" --target i386 -o "${tmp_dir}/std-io-i386.elf"
"${build_dir}/rexc" "${repo_dir}/examples/std_io.rx" --target x86_64 -o "${tmp_dir}/std-io-x86_64.elf"
file "${tmp_dir}/std-io-i386.elf" | grep -F -q 'ELF 32-bit'
file "${tmp_dir}/std-io-x86_64.elf" | grep -F -q 'ELF 64-bit'
```

- [ ] **Step 3: Run smoke to verify red**

Run:

```sh
cmake --build build
tests/assemble_smoke.sh build .
```

Expected: link fails with unresolved `println`, `print`, or `read_line` because the hosted runtime object is not linked yet.

- [ ] **Step 4: Include stdlib metadata in main**

In `src/main.cpp`, include:

```cpp
#include "rexc/stdlib.hpp"
```

- [ ] **Step 5: Add runtime assembly helper**

Add this helper after `assemble_object`:

```cpp
std::string write_and_assemble_hosted_runtime(const std::string &output_path,
                                              rexc::CodegenTarget target)
{
	std::string runtime_assembly = rexc::stdlib::hosted_runtime_assembly(target);
	if (runtime_assembly.empty())
		return "";

	std::string runtime_assembly_path = output_path + ".stdlib.s.tmp";
	std::string runtime_object_path = output_path + ".stdlib.o.tmp";
	write_file(runtime_assembly_path, runtime_assembly);
	try {
		assemble_object(runtime_assembly_path, runtime_object_path, target);
	} catch (...) {
		std::remove(runtime_assembly_path.c_str());
		std::remove(runtime_object_path.c_str());
		throw;
	}
	std::remove(runtime_assembly_path.c_str());
	return runtime_object_path;
}
```

- [ ] **Step 6: Link runtime object for Darwin**

Change `link_darwin_arm64_object` signature:

```cpp
void link_darwin_arm64_object(const std::string &object_path,
                              const std::string &runtime_object_path,
                              const std::string &output_path,
                              rexc::CodegenTarget target)
```

Build the command with the runtime object when present:

```cpp
std::string command = "clang -arch arm64 " + shell_quote(object_path);
if (!runtime_object_path.empty())
	command += " " + shell_quote(runtime_object_path);
command += " -o " + shell_quote(output_path);
run_tool(command, "Darwin link failed");
```

- [ ] **Step 7: Link runtime object for ELF**

Change `link_cross_elf_object` signature:

```cpp
void link_cross_elf_object(const std::string &object_path,
                           const std::string &runtime_object_path,
                           const std::string &output_path,
                           rexc::CodegenTarget target)
```

Append `runtime_object_path` between program object and output command cleanup:

```cpp
std::string command = "x86_64-elf-ld -m " + linker_mode + " -o " +
                      shell_quote(output_path) + " " + shell_quote(startup_object) +
                      " " + shell_quote(object_path);
if (!runtime_object_path.empty())
	command += " " + shell_quote(runtime_object_path);
run_tool(command, "cross ELF link failed");
```

Change `link_host_x86_object` to accept and pass `runtime_object_path`:

```cpp
void link_host_x86_object(const std::string &object_path,
                          const std::string &runtime_object_path,
                          const std::string &output_path,
                          rexc::CodegenTarget target)
```

For host `clang` or `cc` command:

```cpp
std::string command = linker + " " + mode + " " + shell_quote(object_path);
if (!runtime_object_path.empty())
	command += " " + shell_quote(runtime_object_path);
command += " -o " + shell_quote(output_path);
run_tool(command, "host executable link failed");
```

Change `link_executable_object` to accept and pass `runtime_object_path`:

```cpp
void link_executable_object(const std::string &object_path,
                            const std::string &runtime_object_path,
                            const std::string &output_path,
                            rexc::CodegenTarget target)
{
	if (target == rexc::CodegenTarget::ARM64_MACOS) {
		link_darwin_arm64_object(object_path, runtime_object_path, output_path,
		                         target);
		return;
	}
	link_host_x86_object(object_path, runtime_object_path, output_path, target);
}
```

- [ ] **Step 8: Assemble runtime in executable output mode**

In the `OutputMode::Executable` branch in `main`, add a runtime object path:

```cpp
std::string runtime_object_path;
```

After assembling the program object and before linking:

```cpp
runtime_object_path = write_and_assemble_hosted_runtime(options.output_path,
                                                        options.target);
link_executable_object(object_path, runtime_object_path, options.output_path,
                       options.target);
```

In the catch block and after successful link, remove `runtime_object_path` if it is not empty:

```cpp
if (!runtime_object_path.empty())
	remove_if_present(runtime_object_path);
```

- [ ] **Step 9: Verify executable std runtime green**

Run:

```sh
cmake --build build
tests/assemble_smoke.sh build .
```

Expected: on Apple Silicon macOS, `std_io.rx` runs and prints both `hello from rexc` and `echo: friend`; x86 ELF std I/O executables link and pass `file` checks.

- [ ] **Step 10: Commit**

Run:

```sh
git add src/main.cpp tests/assemble_smoke.sh examples/std_io.rx
git commit -m "feat: link hosted std runtime"
```

### Task 7: README Documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add Standard Library docs**

Add this section after `Core Types`:

```markdown
## Standard Library

Rexc's standard library is layered. `core` is the always-available,
target-independent contract layer. `std` is the hosted layer linked into normal
command-line executables. The first `std` milestone exposes a small prelude, so
programs can call standard functions without module syntax:

| Function | Type | Behavior |
| --- | --- | --- |
| `print` | `fn(str) -> i32` | Writes a string to stdout without adding a newline. |
| `println` | `fn(str) -> i32` | Writes a string to stdout followed by `\n`. |
| `read_line` | `fn() -> str` | Reads one stdin line into a runtime-owned 1024-byte buffer and returns it as `str`. |
| `exit` | `fn(i32) -> i32` | Terminates the process with the given status. |

`read_line` strips one trailing newline when present, always null-terminates the
buffer, and overwrites the same buffer on the next `read_line` call.

Example:

```rust
fn main() -> i32 {
    println("name?");
    let name: str = read_line();
    print("hello ");
    println(name);
    return 0;
}
```

Executable builds link the hosted `std` runtime automatically:

```sh
build/rexc examples/std_io.rx -o build/std_io
printf 'friend\n' | build/std_io
```

Assembly-only (`-S`) and object-only (`-c`) builds can reference standard
library symbols, but they do not include the runtime object. Manual final links
must provide `print`, `println`, `read_line`, and `exit`.
```

- [ ] **Step 2: Mention call statements**

In the `Operators And Control Flow` section, add:

```markdown
Function calls can be used as expressions or as statements. Call statements are
intended for side-effecting standard-library functions:

```rust
println("hello");
exit(1);
```
```

- [ ] **Step 3: Verify docs and full suite**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

Expected: all tests pass and diff check reports no whitespace errors.

- [ ] **Step 4: Commit**

Run:

```sh
git add README.md
git commit -m "docs: document rexc standard library"
```

### Task 8: Final Verification

**Files:**
- Verify all changed files

- [ ] **Step 1: Run unit and smoke tests**

Run:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: `rexc_tests`, `parser_architecture`, `cli_smoke`, and `assemble_smoke` all pass.

- [ ] **Step 2: Verify default Darwin std executable**

On Apple Silicon macOS, run:

```sh
build/rexc examples/std_io.rx -o build/std_io
printf 'friend\n' | build/std_io > build/std_io.out
cat build/std_io.out
```

Expected output:

```text
hello from rexc
echo: friend
```

- [ ] **Step 3: Verify x86 ELF std executable links**

On macOS with cross binutils installed, run:

```sh
build/rexc examples/std_io.rx --target i386 -o build/std_io-i386.elf
build/rexc examples/std_io.rx --target x86_64 -o build/std_io-x86_64.elf
file build/std_io-i386.elf build/std_io-x86_64.elf
```

Expected output includes:

```text
ELF 32-bit
ELF 64-bit
```

- [ ] **Step 4: Verify shell syntax and whitespace**

Run:

```sh
sh -n tests/assemble_smoke.sh
git diff --check
```

Expected: no syntax errors and no whitespace errors.
