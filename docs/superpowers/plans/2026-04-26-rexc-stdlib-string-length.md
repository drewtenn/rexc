# Rexc Standard Library Phase Two Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Grow Rexc's first hosted standard library into a more useful command-line prelude with string length/equality, integer printing, integer parsing, and smoke-tested runtime behavior on supported targets.

**Architecture:** Keep the current no-module prelude model and add a small group of compiler-known declarations in `stdlib`. Move the standard-library implementation into an isolated `src/stdlib/` directory, with metadata in one file and target runtime assembly in target-focused files, so the regular compiler pipeline remains separate from hosted library internals. Calls remain ordinary Rexc function calls through sema, IR, and backend codegen; executable links keep using the existing generated hosted runtime object path.

**Tech Stack:** C++17, existing Rexc parser/sema/IR/codegen pipeline, GNU assembler syntax for i386/x86_64 ELF, Darwin ARM64 assembly linked by `clang`, CMake test harness, POSIX shell smoke tests.

---

## Phase Two API

Add these prelude functions:

| Function | Type | Behavior |
| --- | --- | --- |
| `strlen` | `fn(str) -> i32` | Returns the byte length up to the first null terminator. |
| `str_eq` | `fn(str, str) -> bool` | Returns `true` when both null-terminated byte strings have the same bytes. |
| `print_i32` | `fn(i32) -> i32` | Writes a signed decimal integer to stdout without a newline; returns bytes written or a negative error code. |
| `println_i32` | `fn(i32) -> i32` | Writes a signed decimal integer followed by `\n`; returns bytes written or a negative error code. |
| `parse_i32` | `fn(str) -> i32` | Parses optional leading `-` and decimal digits; returns `0` for empty, invalid, or overflow input. |
| `read_i32` | `fn() -> i32` | Calls `read_line`, then `parse_i32` on the runtime-owned line buffer. |

The fallback-to-`0` parsing policy is intentionally simple because Rexc does not yet have `Result`, option types, tuples, references to output values, or structured error handling.

## File Structure

- `include/rexc/stdlib.hpp`: declares the prelude metadata API and hosted runtime assembly API.
- `src/stdlib/stdlib.cpp`: owns prelude function declarations and dispatches to target runtime assembly builders.
- `src/stdlib/sys/runtime.hpp`: privately declares target runtime assembly builders for the isolated stdlib implementation.
- `src/stdlib/sys/runtime_i386.cpp`: owns i386 ELF hosted runtime adapter assembly text.
- `src/stdlib/sys/runtime_x86_64.cpp`: owns x86_64 ELF hosted runtime adapter assembly text.
- `src/stdlib/sys/runtime_arm64_macos.cpp`: owns Darwin ARM64 hosted runtime adapter assembly text.
- `tests/stdlib_tests.cpp`: checks prelude declarations and target runtime symbol availability.
- `tests/sema_tests.cpp`: checks type-checking and diagnostics for the expanded prelude.
- `tests/codegen_tests.cpp`: checks i386/x86_64 calls to new prelude functions.
- `tests/codegen_arm64_tests.cpp`: checks Darwin ARM64 calls to new prelude functions.
- `tests/cli_smoke.sh`: checks assembly output for all targets and native executable behavior where runnable.
- `examples/std_strings.rx`: demonstrates `strlen` and `str_eq`.
- `examples/std_numbers.rx`: demonstrates integer output and parsing.
- `README.md`: documents the expanded prelude.
- `docs/superpowers/specs/2026-04-25-rexc-layered-standard-library-design.md`: records the Phase Two API as the next design increment.

`include/rexc/stdlib.hpp` remains in `include/rexc/` because sema, lowering, and the CLI need a stable compiler-facing API. All implementation details for the standard library live under `src/stdlib/`; avoid adding future hosted stdlib code directly beside compiler pipeline files such as `src/sema.cpp`, `src/lower_ir.cpp`, or `src/codegen_x86.cpp`.

### Task 1: Isolate And Split Standard Library Implementation

**Files:**
- Modify: `include/rexc/stdlib.hpp`
- Move: `src/stdlib.cpp` to `src/stdlib/stdlib.cpp`
- Create: `src/stdlib/sys/runtime.hpp`
- Create: `src/stdlib/sys/runtime_i386.cpp`
- Create: `src/stdlib/sys/runtime_x86_64.cpp`
- Create: `src/stdlib/sys/runtime_arm64_macos.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/stdlib_tests.cpp`

- [ ] **Step 1: Write a guard test for current runtime dispatch**

Append this test to `tests/stdlib_tests.cpp`:

```cpp
TEST_CASE(stdlib_runtime_dispatch_returns_different_target_assemblies)
{
	auto i386 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::I386);
	auto x86_64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::X86_64);
	auto arm64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::ARM64_MACOS);

	REQUIRE(i386.find("int $0x80") != std::string::npos);
	REQUIRE(x86_64.find("syscall") != std::string::npos);
	REQUIRE(arm64.find("bl _write") != std::string::npos);
	REQUIRE(i386 != x86_64);
	REQUIRE(x86_64 != arm64);
}
```

- [ ] **Step 2: Run tests before refactor**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: tests pass before the split, proving the refactor has a behavioral safety net.

- [ ] **Step 3: Create the isolated stdlib source directory**

Move the current metadata/runtime file into a dedicated implementation directory:

```sh
mkdir -p src/stdlib
git mv src/stdlib.cpp src/stdlib/stdlib.cpp
```

Expected: the existing `src/stdlib.cpp` path is gone, and the metadata file now lives at `src/stdlib/stdlib.cpp`.

- [ ] **Step 4: Add private target runtime function declarations**

Keep `include/rexc/stdlib.hpp` limited to the public compiler-facing API:

```cpp
const std::vector<FunctionDecl> &prelude_functions();
const FunctionDecl *find_prelude_function(const std::string &name);
std::string hosted_runtime_assembly(CodegenTarget target);
```

Create `src/stdlib/sys/runtime.hpp`:

```cpp
#pragma once

#include <string>

namespace rexc::stdlib {

std::string i386_hosted_runtime_assembly();
std::string x86_64_hosted_runtime_assembly();
std::string arm64_macos_hosted_runtime_assembly();

} // namespace rexc::stdlib
```

- [ ] **Step 5: Move i386 assembly into its own file**

Create `src/stdlib/sys/runtime_i386.cpp`:

```cpp
#include "runtime.hpp"

#include <string>

namespace rexc::stdlib {

std::string i386_hosted_runtime_assembly()
{
	return R"(.section .rodata
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
	pushl %ebx
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
	popl %ebx
	leave
	ret
.globl println
println:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	subl $4, %esp
	pushl 8(%ebp)
	call print
	addl $4, %esp
	movl %eax, -8(%ebp)
	movl $4, %eax
	movl $1, %ebx
	movl $.Lrexc_newline, %ecx
	movl $1, %edx
	int $0x80
	addl -8(%ebp), %eax
	addl $4, %esp
	popl %ebx
	leave
	ret
.globl read_line
read_line:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	xorl %edx, %edx
.Lrexc_i386_read_loop:
	cmpl $1023, %edx
	jae .Lrexc_i386_read_done
	movl $3, %eax
	movl $0, %ebx
	movl $.Lrexc_read_line_buffer, %ecx
	addl %edx, %ecx
	pushl %edx
	movl $1, %edx
	int $0x80
	popl %edx
	cmpl $0, %eax
	jle .Lrexc_i386_read_done
	cmpb $10, .Lrexc_read_line_buffer(%edx)
	je .Lrexc_i386_read_done
	incl %edx
	jmp .Lrexc_i386_read_loop
.Lrexc_i386_read_done:
	movb $0, .Lrexc_read_line_buffer(%edx)
	movl $.Lrexc_read_line_buffer, %eax
	popl %ebx
	leave
	ret
.globl exit
exit:
	movl 4(%esp), %ebx
	movl $1, %eax
	int $0x80
)";
}

} // namespace rexc::stdlib
```

Remove the old anonymous `i386_hosted_runtime_assembly()` helper from `src/stdlib/stdlib.cpp`.

- [ ] **Step 6: Move x86_64 assembly into its own file**

Create `src/stdlib/sys/runtime_x86_64.cpp` by moving the current `x86_64_hosted_runtime_assembly()` function body out of `src/stdlib/stdlib.cpp`. Keep the same signature:

```cpp
#include "runtime.hpp"

#include <string>

namespace rexc::stdlib {

std::string x86_64_hosted_runtime_assembly()
{
	return R"(.section .rodata
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
	xorq %r8, %r8
.Lrexc_x64_read_loop:
	cmpq $1023, %r8
	jae .Lrexc_x64_read_done
	movq $0, %rax
	movq $0, %rdi
	leaq .Lrexc_read_line_buffer(%rip), %rsi
	addq %r8, %rsi
	movq $1, %rdx
	syscall
	cmpq $0, %rax
	jle .Lrexc_x64_read_done
	leaq .Lrexc_read_line_buffer(%rip), %rsi
	cmpb $10, (%rsi,%r8)
	je .Lrexc_x64_read_done
	incq %r8
	jmp .Lrexc_x64_read_loop
.Lrexc_x64_read_done:
	leaq .Lrexc_read_line_buffer(%rip), %rsi
	movb $0, (%rsi,%r8)
	movq %rsi, %rax
	ret
.globl exit
exit:
	movq $60, %rax
	syscall
)";
}

} // namespace rexc::stdlib
```

Remove the old anonymous x86_64 helper from `src/stdlib/stdlib.cpp`.

- [ ] **Step 7: Move ARM64 macOS assembly into its own file**

Create `src/stdlib/sys/runtime_arm64_macos.cpp` by moving the current `arm64_macos_hosted_runtime_assembly()` function body out of `src/stdlib/stdlib.cpp`. Keep the same signature:

```cpp
#include "runtime.hpp"

#include <string>

namespace rexc::stdlib {

std::string arm64_macos_hosted_runtime_assembly()
{
	return R"(.cstring
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
	adrp x1, Lrexc_read_line_buffer@PAGE
	add x1, x1, Lrexc_read_line_buffer@PAGEOFF
	mov x19, x1
	mov x20, #0
Lrexc_arm64_read_loop:
	cmp x20, #1023
	b.hs Lrexc_arm64_read_done
	mov x0, #0
	add x1, x19, x20
	mov x2, #1
	bl _read
	cmp x0, #0
	b.le Lrexc_arm64_read_done
	ldrb w3, [x19, x20]
	cmp w3, #10
	b.eq Lrexc_arm64_read_done
	add x20, x20, #1
	b Lrexc_arm64_read_loop
Lrexc_arm64_read_done:
	strb wzr, [x19, x20]
	mov x0, x19
	ldr x20, [sp, #24]
	ldr x19, [sp, #16]
	ldp x29, x30, [sp], #48
	ret
)";
}

} // namespace rexc::stdlib
```

Remove the old anonymous ARM64 helper from `src/stdlib/stdlib.cpp`.

- [ ] **Step 8: Keep `src/stdlib/stdlib.cpp` focused on metadata and dispatch**

After removing the runtime helper bodies, `src/stdlib/stdlib.cpp` should contain only the primitive type helpers, `prelude_functions`, `find_prelude_function`, and:

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

- [ ] **Step 9: Replace the old stdlib source path in CMake**

In `CMakeLists.txt`, replace the old `src/stdlib.cpp` entry in `add_library(rexc_core ...)` with the isolated stdlib implementation files:

```cmake
	src/stdlib/stdlib.cpp
	src/stdlib/sys/runtime_i386.cpp
	src/stdlib/sys/runtime_x86_64.cpp
	src/stdlib/sys/runtime_arm64_macos.cpp
```

- [ ] **Step 10: Verify refactor**

Run:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all existing tests pass.

- [ ] **Step 11: Commit**

Run:

```sh
git add include/rexc/stdlib.hpp src/stdlib/stdlib.cpp src/stdlib/sys/runtime.hpp src/stdlib/sys/runtime_i386.cpp src/stdlib/sys/runtime_x86_64.cpp src/stdlib/sys/runtime_arm64_macos.cpp CMakeLists.txt tests/stdlib_tests.cpp
git commit -m "refactor: split hosted std runtime assembly"
```

### Task 2: Add String Prelude Declarations

**Files:**
- Modify: `src/stdlib/stdlib.cpp`
- Test: `tests/stdlib_tests.cpp`
- Test: `tests/sema_tests.cpp`

- [ ] **Step 1: Extend declaration tests for `strlen` and `str_eq`**

In `tests/stdlib_tests.cpp`, update `stdlib_declares_prelude_functions` so it fetches:

```cpp
auto strlen = rexc::stdlib::find_prelude_function("strlen");
auto str_eq = rexc::stdlib::find_prelude_function("str_eq");
```

Add null checks:

```cpp
REQUIRE(strlen != nullptr);
REQUIRE(str_eq != nullptr);
```

Add signature checks after `read_line`:

```cpp
REQUIRE_EQ(strlen->parameters.size(), std::size_t(1));
REQUIRE_EQ(strlen->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
REQUIRE_EQ(strlen->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
REQUIRE_EQ(str_eq->parameters.size(), std::size_t(2));
REQUIRE_EQ(str_eq->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
REQUIRE_EQ(str_eq->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
REQUIRE_EQ(str_eq->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
```

- [ ] **Step 2: Add semantic tests**

Append these tests to `tests/sema_tests.cpp`:

```cpp
TEST_CASE(sema_accepts_std_prelude_string_helpers)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 {\n"
		"  let n: i32 = strlen(\"hello\");\n"
		"  if str_eq(\"a\", \"a\") { return n; } else { return 0; }\n"
		"}\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_std_prelude_strlen_wrong_argument_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { return strlen(7); }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("argument type mismatch: expected 'str' but got 'i32'") != std::string::npos);
}

TEST_CASE(sema_rejects_std_prelude_str_eq_wrong_argument_count)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { if str_eq(\"a\") { return 1; } return 0; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("function 'str_eq' expected 2 arguments but got 1") != std::string::npos);
}
```

- [ ] **Step 3: Run tests to verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: declaration and semantic tests fail because `strlen` and `str_eq` are not in the prelude metadata yet.

- [ ] **Step 4: Add string declarations**

In `src/stdlib/stdlib.cpp`, add these declarations to `prelude_functions()` after `read_line`:

```cpp
FunctionDecl{"strlen", {str_type()}, i32_type()},
FunctionDecl{"str_eq", {str_type(), str_type()}, PrimitiveType{PrimitiveKind::Bool}},
```

The vector should become:

```cpp
static const std::vector<FunctionDecl> functions{
	FunctionDecl{"print", {str_type()}, i32_type()},
	FunctionDecl{"println", {str_type()}, i32_type()},
	FunctionDecl{"read_line", {}, str_type()},
	FunctionDecl{"strlen", {str_type()}, i32_type()},
	FunctionDecl{"str_eq", {str_type(), str_type()}, PrimitiveType{PrimitiveKind::Bool}},
	FunctionDecl{"exit", {i32_type()}, i32_type()},
};
```

- [ ] **Step 5: Verify metadata and sema**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: new metadata and sema tests pass.

- [ ] **Step 6: Commit**

Run:

```sh
git add src/stdlib/stdlib.cpp tests/stdlib_tests.cpp tests/sema_tests.cpp
git commit -m "feat: declare std string helpers"
```

### Task 3: Implement String Runtime Helpers

**Files:**
- Modify: `src/stdlib/sys/runtime_i386.cpp`
- Modify: `src/stdlib/sys/runtime_x86_64.cpp`
- Modify: `src/stdlib/sys/runtime_arm64_macos.cpp`
- Test: `tests/stdlib_tests.cpp`

- [ ] **Step 1: Extend runtime symbol tests**

In `tests/stdlib_tests.cpp`, add these assertions to `stdlib_emits_hosted_runtime_symbols`.

For i386:

```cpp
REQUIRE(contains(i386, "strlen:"));
REQUIRE(contains(i386, "str_eq:"));
REQUIRE(contains(i386, ".Lrexc_i386_strlen_loop:"));
REQUIRE(contains(i386, ".Lrexc_i386_str_eq_loop:"));
```

For x86_64:

```cpp
REQUIRE(contains(x86_64, "strlen:"));
REQUIRE(contains(x86_64, "str_eq:"));
REQUIRE(contains(x86_64, ".Lrexc_x64_strlen_loop:"));
REQUIRE(contains(x86_64, ".Lrexc_x64_str_eq_loop:"));
```

For ARM64 macOS:

```cpp
REQUIRE(contains(arm64, "_strlen:"));
REQUIRE(contains(arm64, "_str_eq:"));
REQUIRE(contains(arm64, "Lrexc_arm64_strlen_loop:"));
REQUIRE(contains(arm64, "Lrexc_arm64_str_eq_loop:"));
```

- [ ] **Step 2: Run tests to verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: runtime symbol tests fail because the target runtime assembly does not define the string helper labels.

- [ ] **Step 3: Add i386 string helper assembly**

In `src/stdlib/sys/runtime_i386.cpp`, insert this block after `read_line` and before `exit`:

```asm
.globl strlen
strlen:
	pushl %ebp
	movl %esp, %ebp
	movl 8(%ebp), %ecx
	xorl %eax, %eax
.Lrexc_i386_strlen_loop:
	cmpb $0, (%ecx,%eax)
	je .Lrexc_i386_strlen_done
	incl %eax
	jmp .Lrexc_i386_strlen_loop
.Lrexc_i386_strlen_done:
	leave
	ret
.globl str_eq
str_eq:
	pushl %ebp
	movl %esp, %ebp
	movl 8(%ebp), %ecx
	movl 12(%ebp), %edx
.Lrexc_i386_str_eq_loop:
	movb (%ecx), %al
	movb (%edx), %ah
	cmpb %ah, %al
	jne .Lrexc_i386_str_eq_false
	cmpb $0, %al
	je .Lrexc_i386_str_eq_true
	incl %ecx
	incl %edx
	jmp .Lrexc_i386_str_eq_loop
.Lrexc_i386_str_eq_true:
	movl $1, %eax
	leave
	ret
.Lrexc_i386_str_eq_false:
	xorl %eax, %eax
	leave
	ret
```

- [ ] **Step 4: Add x86_64 string helper assembly**

In `src/stdlib/sys/runtime_x86_64.cpp`, insert this block after `read_line` and before `exit`:

```asm
.globl strlen
strlen:
	xorq %rax, %rax
.Lrexc_x64_strlen_loop:
	cmpb $0, (%rdi,%rax)
	je .Lrexc_x64_strlen_done
	incq %rax
	jmp .Lrexc_x64_strlen_loop
.Lrexc_x64_strlen_done:
	ret
.globl str_eq
str_eq:
.Lrexc_x64_str_eq_loop:
	movb (%rdi), %al
	movb (%rsi), %cl
	cmpb %cl, %al
	jne .Lrexc_x64_str_eq_false
	cmpb $0, %al
	je .Lrexc_x64_str_eq_true
	incq %rdi
	incq %rsi
	jmp .Lrexc_x64_str_eq_loop
.Lrexc_x64_str_eq_true:
	movq $1, %rax
	ret
.Lrexc_x64_str_eq_false:
	xorq %rax, %rax
	ret
```

- [ ] **Step 5: Add ARM64 macOS string helper assembly**

In `src/stdlib/sys/runtime_arm64_macos.cpp`, insert this block after `_read_line`:

```asm
.globl _strlen
.p2align 2
_strlen:
	mov x1, x0
	mov x0, #0
Lrexc_arm64_strlen_loop:
	ldrb w2, [x1, x0]
	cbz w2, Lrexc_arm64_strlen_done
	add x0, x0, #1
	b Lrexc_arm64_strlen_loop
Lrexc_arm64_strlen_done:
	ret
.globl _str_eq
.p2align 2
_str_eq:
Lrexc_arm64_str_eq_loop:
	ldrb w2, [x0]
	ldrb w3, [x1]
	cmp w2, w3
	b.ne Lrexc_arm64_str_eq_false
	cbz w2, Lrexc_arm64_str_eq_true
	add x0, x0, #1
	add x1, x1, #1
	b Lrexc_arm64_str_eq_loop
Lrexc_arm64_str_eq_true:
	mov x0, #1
	ret
Lrexc_arm64_str_eq_false:
	mov x0, #0
	ret
```

- [ ] **Step 6: Verify runtime helper tests**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: runtime symbol tests pass.

- [ ] **Step 7: Commit**

Run:

```sh
git add src/stdlib/sys/runtime_i386.cpp src/stdlib/sys/runtime_x86_64.cpp src/stdlib/sys/runtime_arm64_macos.cpp tests/stdlib_tests.cpp
git commit -m "feat: add std string runtime helpers"
```

### Task 4: Cover String Codegen And CLI Paths

**Files:**
- Modify: `tests/codegen_tests.cpp`
- Modify: `tests/codegen_arm64_tests.cpp`
- Modify: `tests/cli_smoke.sh`
- Create: `examples/std_strings.rx`

- [ ] **Step 1: Add x86 codegen tests**

Append these tests to `tests/codegen_tests.cpp`:

```cpp
TEST_CASE(codegen_i386_emits_std_string_helper_calls)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { if str_eq(\"hi\", \"hi\") { return strlen(\"hello\"); } return 0; }\n");

	REQUIRE(assembly.find("call str_eq") != std::string::npos);
	REQUIRE(assembly.find("call strlen") != std::string::npos);
	REQUIRE(assembly.find("addl $8, %esp") != std::string::npos);
	REQUIRE(assembly.find("addl $4, %esp") != std::string::npos);
}

TEST_CASE(codegen_x86_64_emits_std_string_helper_calls)
{
	auto assembly = compile_to_assembly(
		"fn main() -> i32 { if str_eq(\"hi\", \"hi\") { return strlen(\"hello\"); } return 0; }\n",
		rexc::CodegenTarget::X86_64);

	REQUIRE(assembly.find("call str_eq") != std::string::npos);
	REQUIRE(assembly.find("call strlen") != std::string::npos);
	REQUIRE(assembly.find("popq %rdi") != std::string::npos);
	REQUIRE(assembly.find("popq %rsi") != std::string::npos);
}
```

- [ ] **Step 2: Add ARM64 codegen test**

Append this test to `tests/codegen_arm64_tests.cpp`:

```cpp
TEST_CASE(codegen_arm64_macos_emits_std_string_helper_calls)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { if str_eq(\"hi\", \"hi\") { return strlen(\"hello\"); } return 0; }\n");

	REQUIRE(assembly.find("bl _str_eq") != std::string::npos);
	REQUIRE(assembly.find("bl _strlen") != std::string::npos);
}
```

- [ ] **Step 3: Add string example**

Create `examples/std_strings.rx`:

```rust
fn main() -> i32 {
    if str_eq("rexc", "rexc") {
        return strlen("hello");
    }
    return 0;
}
```

- [ ] **Step 4: Add string example CLI assembly smoke**

In `tests/cli_smoke.sh`, add this block:

```sh
"${build_dir}/rexc" "${repo_dir}/examples/std_strings.rx" --target i386 -S -o "${tmp_dir}/std-strings32.s"
grep -F -q 'call str_eq' "${tmp_dir}/std-strings32.s"
grep -F -q 'call strlen' "${tmp_dir}/std-strings32.s"

"${build_dir}/rexc" "${repo_dir}/examples/std_strings.rx" --target x86_64 -S -o "${tmp_dir}/std-strings64.s"
grep -F -q 'call str_eq' "${tmp_dir}/std-strings64.s"
grep -F -q 'call strlen' "${tmp_dir}/std-strings64.s"

"${build_dir}/rexc" "${repo_dir}/examples/std_strings.rx" --target arm64-macos -S -o "${tmp_dir}/std-strings-arm64.s"
grep -F -q 'bl _str_eq' "${tmp_dir}/std-strings-arm64.s"
grep -F -q 'bl _strlen' "${tmp_dir}/std-strings-arm64.s"
```

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

Run:

```sh
git add tests/codegen_tests.cpp tests/codegen_arm64_tests.cpp tests/cli_smoke.sh examples/std_strings.rx
git commit -m "test: cover std string helper calls"
```

### Task 5: Add Numeric Prelude Declarations

**Files:**
- Modify: `src/stdlib/stdlib.cpp`
- Test: `tests/stdlib_tests.cpp`
- Test: `tests/sema_tests.cpp`

- [ ] **Step 1: Extend declaration tests for numeric helpers**

In `tests/stdlib_tests.cpp`, update `stdlib_declares_prelude_functions` so it fetches:

```cpp
auto print_i32 = rexc::stdlib::find_prelude_function("print_i32");
auto println_i32 = rexc::stdlib::find_prelude_function("println_i32");
auto parse_i32 = rexc::stdlib::find_prelude_function("parse_i32");
auto read_i32 = rexc::stdlib::find_prelude_function("read_i32");
```

Add null checks:

```cpp
REQUIRE(print_i32 != nullptr);
REQUIRE(println_i32 != nullptr);
REQUIRE(parse_i32 != nullptr);
REQUIRE(read_i32 != nullptr);
```

Add these signature checks after the string helper checks:

```cpp
REQUIRE_EQ(print_i32->parameters.size(), std::size_t(1));
REQUIRE_EQ(print_i32->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
REQUIRE_EQ(print_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
REQUIRE_EQ(println_i32->parameters.size(), std::size_t(1));
REQUIRE_EQ(println_i32->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
REQUIRE_EQ(println_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
REQUIRE_EQ(parse_i32->parameters.size(), std::size_t(1));
REQUIRE_EQ(parse_i32->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
REQUIRE_EQ(parse_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
REQUIRE_EQ(read_i32->parameters.size(), std::size_t(0));
REQUIRE_EQ(read_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
```

- [ ] **Step 2: Add semantic tests**

Append these tests to `tests/sema_tests.cpp`:

```cpp
TEST_CASE(sema_accepts_std_prelude_numeric_helpers)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
		"fn main() -> i32 {\n"
		"  print_i32(42);\n"
		"  println_i32(parse_i32(\"-7\"));\n"
		"  return read_i32();\n"
		"}\n",
		diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_std_prelude_print_i32_wrong_argument_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { print_i32(\"7\"); return 0; }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("argument type mismatch: expected 'i32' but got 'str'") != std::string::npos);
}

TEST_CASE(sema_rejects_std_prelude_parse_i32_wrong_argument_type)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze("fn main() -> i32 { return parse_i32(7); }\n", diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("argument type mismatch: expected 'str' but got 'i32'") != std::string::npos);
}
```

- [ ] **Step 3: Run tests to verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: tests fail because numeric helpers are not declared.

- [ ] **Step 4: Add numeric declarations**

In `src/stdlib/stdlib.cpp`, add these declarations before `exit`:

```cpp
FunctionDecl{"print_i32", {i32_type()}, i32_type()},
FunctionDecl{"println_i32", {i32_type()}, i32_type()},
FunctionDecl{"parse_i32", {str_type()}, i32_type()},
FunctionDecl{"read_i32", {}, i32_type()},
```

- [ ] **Step 5: Verify metadata and sema**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: new metadata and sema tests pass.

- [ ] **Step 6: Commit**

Run:

```sh
git add src/stdlib/stdlib.cpp tests/stdlib_tests.cpp tests/sema_tests.cpp
git commit -m "feat: declare std numeric helpers"
```

### Task 6: Implement Numeric Output Runtime Helpers

**Files:**
- Modify: `src/stdlib/sys/runtime_i386.cpp`
- Modify: `src/stdlib/sys/runtime_x86_64.cpp`
- Modify: `src/stdlib/sys/runtime_arm64_macos.cpp`
- Test: `tests/stdlib_tests.cpp`

- [ ] **Step 1: Extend runtime symbol tests for output helpers**

In `tests/stdlib_tests.cpp`, add these checks to `stdlib_emits_hosted_runtime_symbols`.

For i386:

```cpp
REQUIRE(contains(i386, "print_i32:"));
REQUIRE(contains(i386, "println_i32:"));
REQUIRE(contains(i386, ".Lrexc_i386_i32_digits:"));
```

For x86_64:

```cpp
REQUIRE(contains(x86_64, "print_i32:"));
REQUIRE(contains(x86_64, "println_i32:"));
REQUIRE(contains(x86_64, ".Lrexc_x64_i32_digits:"));
```

For ARM64 macOS:

```cpp
REQUIRE(contains(arm64, "_print_i32:"));
REQUIRE(contains(arm64, "_println_i32:"));
REQUIRE(contains(arm64, "Lrexc_arm64_i32_digits:"));
```

- [ ] **Step 2: Run tests to verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: runtime symbol tests fail because numeric output helpers are not defined.

- [ ] **Step 3: Add decimal buffer storage**

In each runtime assembly string, add a 16-byte buffer for `i32` decimal output:

i386 and x86_64, in `.bss` after `.Lrexc_read_line_buffer`:

```asm
.Lrexc_i32_buffer:
	.zero 16
```

ARM64 macOS, after the existing `.zerofill` line:

```asm
.zerofill __DATA,__bss,Lrexc_i32_buffer,16,4
```

- [ ] **Step 4: Implement i386 `print_i32` and `println_i32`**

In `src/stdlib/sys/runtime_i386.cpp`, add a `print_i32` routine that:

- Handles `0` by writing one byte `'0'`.
- Handles negative values by writing `'-'`, then converting the absolute value using signed division by `10`.
- Fills `.Lrexc_i32_buffer` from the end backward.
- Calls Linux `write` with the computed buffer pointer and length.
- Returns the total byte count written.

Use these labels exactly so tests and future maintainers have stable anchors:

```asm
.Lrexc_i386_i32_digits:
.Lrexc_i386_print_i32_zero:
.Lrexc_i386_print_i32_negative:
.Lrexc_i386_print_i32_write:
```

Add `println_i32` immediately after it. It should call `print_i32`, write `.Lrexc_newline`, add the newline write result, and return the total byte count.

- [ ] **Step 5: Implement x86_64 `print_i32` and `println_i32`**

In `src/stdlib/sys/runtime_x86_64.cpp`, add equivalent routines using System V arguments and Linux `syscall`.

Use these labels exactly:

```asm
.Lrexc_x64_i32_digits:
.Lrexc_x64_print_i32_zero:
.Lrexc_x64_print_i32_negative:
.Lrexc_x64_print_i32_write:
```

Keep `%rbx` untouched or save/restore it if used. Prefer caller-clobbered registers (`%rax`, `%rcx`, `%rdx`, `%r8`, `%r9`, `%r10`, `%r11`) for conversion.

- [ ] **Step 6: Implement ARM64 macOS `print_i32` and `println_i32`**

In `src/stdlib/sys/runtime_arm64_macos.cpp`, add equivalent routines using Apple ARM64 calling conventions and `_write`.

Use these labels exactly:

```asm
Lrexc_arm64_i32_digits:
Lrexc_arm64_print_i32_zero:
Lrexc_arm64_print_i32_negative:
Lrexc_arm64_print_i32_write:
```

Save and restore any callee-saved registers you use (`x19`-`x28`). Keep the stack 16-byte aligned before calls to `_write` and `_print_i32`.

- [ ] **Step 7: Verify runtime symbol tests**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: runtime symbol tests pass.

- [ ] **Step 8: Commit**

Run:

```sh
git add src/stdlib/sys/runtime_i386.cpp src/stdlib/sys/runtime_x86_64.cpp src/stdlib/sys/runtime_arm64_macos.cpp tests/stdlib_tests.cpp
git commit -m "feat: add std i32 output runtime"
```

### Task 7: Implement Numeric Parsing Runtime Helpers

**Files:**
- Modify: `src/stdlib/sys/runtime_i386.cpp`
- Modify: `src/stdlib/sys/runtime_x86_64.cpp`
- Modify: `src/stdlib/sys/runtime_arm64_macos.cpp`
- Test: `tests/stdlib_tests.cpp`

- [ ] **Step 1: Extend runtime symbol tests for parsing helpers**

In `tests/stdlib_tests.cpp`, add these checks to `stdlib_emits_hosted_runtime_symbols`.

For i386:

```cpp
REQUIRE(contains(i386, "parse_i32:"));
REQUIRE(contains(i386, "read_i32:"));
REQUIRE(contains(i386, ".Lrexc_i386_parse_i32_loop:"));
```

For x86_64:

```cpp
REQUIRE(contains(x86_64, "parse_i32:"));
REQUIRE(contains(x86_64, "read_i32:"));
REQUIRE(contains(x86_64, ".Lrexc_x64_parse_i32_loop:"));
```

For ARM64 macOS:

```cpp
REQUIRE(contains(arm64, "_parse_i32:"));
REQUIRE(contains(arm64, "_read_i32:"));
REQUIRE(contains(arm64, "Lrexc_arm64_parse_i32_loop:"));
```

- [ ] **Step 2: Run tests to verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: runtime symbol tests fail because parsing helpers are not defined.

- [ ] **Step 3: Implement `parse_i32` in each runtime**

Implement `parse_i32` with this exact behavior on i386, x86_64, and ARM64:

```text
input ""        -> 0
input "-"       -> 0
input "0"       -> 0
input "42"      -> 42
input "-42"     -> -42
input "12x"     -> 0
input "x12"     -> 0
input "2147483647"  -> 2147483647
input "2147483648"  -> 0
input "-2147483648" -> -2147483648
input "-2147483649" -> 0
```

Use these loop labels exactly:

```asm
.Lrexc_i386_parse_i32_loop:
.Lrexc_x64_parse_i32_loop:
Lrexc_arm64_parse_i32_loop:
```

Overflow rule: before multiplying by 10 and adding the next digit, reject if the accumulated positive magnitude would exceed `2147483647` for positive numbers or `2147483648` for negative numbers.

- [ ] **Step 4: Implement `read_i32` in each runtime**

Implement `read_i32` as a tiny wrapper:

i386:

```asm
.globl read_i32
read_i32:
	call read_line
	pushl %eax
	call parse_i32
	addl $4, %esp
	ret
```

x86_64:

```asm
.globl read_i32
read_i32:
	call read_line
	movq %rax, %rdi
	call parse_i32
	ret
```

ARM64 macOS:

```asm
.globl _read_i32
.p2align 2
_read_i32:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	bl _read_line
	bl _parse_i32
	ldp x29, x30, [sp], #16
	ret
```

- [ ] **Step 5: Verify runtime symbol tests**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: runtime symbol tests pass.

- [ ] **Step 6: Commit**

Run:

```sh
git add src/stdlib/sys/runtime_i386.cpp src/stdlib/sys/runtime_x86_64.cpp src/stdlib/sys/runtime_arm64_macos.cpp tests/stdlib_tests.cpp
git commit -m "feat: add std i32 parsing runtime"
```

### Task 8: Cover Numeric Codegen And CLI Paths

**Files:**
- Modify: `tests/codegen_tests.cpp`
- Modify: `tests/codegen_arm64_tests.cpp`
- Modify: `tests/cli_smoke.sh`
- Create: `examples/std_numbers.rx`

- [ ] **Step 1: Add x86 codegen tests**

Append these tests to `tests/codegen_tests.cpp`:

```cpp
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
```

- [ ] **Step 2: Add ARM64 codegen test**

Append this test to `tests/codegen_arm64_tests.cpp`:

```cpp
TEST_CASE(codegen_arm64_macos_emits_std_numeric_helper_calls)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { print_i32(42); println_i32(parse_i32(\"-7\")); return read_i32(); }\n");

	REQUIRE(assembly.find("bl _print_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _println_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _parse_i32") != std::string::npos);
	REQUIRE(assembly.find("bl _read_i32") != std::string::npos);
}
```

- [ ] **Step 3: Add numeric example**

Create `examples/std_numbers.rx`:

```rust
fn main() -> i32 {
    println("number?");
    let value: i32 = read_i32();
    print("double: ");
    println_i32(value + value);
    return parse_i32("-7") + 12;
}
```

- [ ] **Step 4: Add CLI assembly smoke checks**

In `tests/cli_smoke.sh`, add:

```sh
"${build_dir}/rexc" "${repo_dir}/examples/std_numbers.rx" --target i386 -S -o "${tmp_dir}/std-numbers32.s"
grep -F -q 'call read_i32' "${tmp_dir}/std-numbers32.s"
grep -F -q 'call println_i32' "${tmp_dir}/std-numbers32.s"
grep -F -q 'call parse_i32' "${tmp_dir}/std-numbers32.s"

"${build_dir}/rexc" "${repo_dir}/examples/std_numbers.rx" --target x86_64 -S -o "${tmp_dir}/std-numbers64.s"
grep -F -q 'call read_i32' "${tmp_dir}/std-numbers64.s"
grep -F -q 'call println_i32' "${tmp_dir}/std-numbers64.s"
grep -F -q 'call parse_i32' "${tmp_dir}/std-numbers64.s"

"${build_dir}/rexc" "${repo_dir}/examples/std_numbers.rx" --target arm64-macos -S -o "${tmp_dir}/std-numbers-arm64.s"
grep -F -q 'bl _read_i32' "${tmp_dir}/std-numbers-arm64.s"
grep -F -q 'bl _println_i32' "${tmp_dir}/std-numbers-arm64.s"
grep -F -q 'bl _parse_i32' "${tmp_dir}/std-numbers-arm64.s"
```

- [ ] **Step 5: Run tests**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

Run:

```sh
git add tests/codegen_tests.cpp tests/codegen_arm64_tests.cpp tests/cli_smoke.sh examples/std_numbers.rx
git commit -m "test: cover std numeric helper calls"
```

### Task 9: Add Native Executable Runtime Smoke Tests

**Files:**
- Modify: `tests/cli_smoke.sh`

- [ ] **Step 1: Add native ARM64 macOS string executable smoke**

In `tests/cli_smoke.sh`, add this guarded block near the end:

```sh
if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
    "${build_dir}/rexc" "${repo_dir}/examples/std_strings.rx" --target arm64-macos -o "${tmp_dir}/std-strings-arm64"
    test -x "${tmp_dir}/std-strings-arm64"
    set +e
    "${tmp_dir}/std-strings-arm64" > "${tmp_dir}/std-strings-arm64.out"
    status=$?
    set -e
    test "$status" -eq 5
    grep -F -q 'strings' "${tmp_dir}/std-strings-arm64.out"
    grep -F -q '5' "${tmp_dir}/std-strings-arm64.out"
fi
```

- [ ] **Step 2: Add native ARM64 macOS numeric executable smoke**

Add a second guarded block after the string executable check:

```sh
if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
    "${build_dir}/rexc" "${repo_dir}/examples/std_numbers.rx" --target arm64-macos -o "${tmp_dir}/std-numbers-arm64"
    test -x "${tmp_dir}/std-numbers-arm64"
    set +e
    printf '21\n' | "${tmp_dir}/std-numbers-arm64" > "${tmp_dir}/std-numbers-arm64.out"
    status=$?
    set -e
    test "$status" -eq 5
    grep -F -q 'number?' "${tmp_dir}/std-numbers-arm64.out"
    grep -F -q 'double: 42' "${tmp_dir}/std-numbers-arm64.out"
fi
```

- [ ] **Step 3: Run native smoke where supported**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R cli_smoke
```

Expected on Apple Silicon macOS: both native executable smoke checks pass. Expected elsewhere: the guarded native block is skipped.

- [ ] **Step 4: Commit**

Run:

```sh
git add tests/cli_smoke.sh
git commit -m "test: smoke std phase two executables"
```

### Task 10: Document Standard Library Phase Two

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-04-25-rexc-layered-standard-library-design.md`

- [ ] **Step 1: Update README prelude table**

In `README.md`, extend the `Standard Library` table with these rows after `read_line`:

```markdown
| `strlen` | `fn(str) -> i32` | Returns the byte length of a null-terminated string. |
| `str_eq` | `fn(str, str) -> bool` | Compares two null-terminated byte strings for equality. |
| `print_i32` | `fn(i32) -> i32` | Writes a signed decimal integer without adding a newline. |
| `println_i32` | `fn(i32) -> i32` | Writes a signed decimal integer followed by `\n`. |
| `parse_i32` | `fn(str) -> i32` | Parses a signed decimal integer, returning `0` for invalid or overflow input. |
| `read_i32` | `fn() -> i32` | Reads one input line and parses it as `i32`. |
```

- [ ] **Step 2: Add behavior notes**

After the existing `read_line` paragraph in `README.md`, add:

```markdown
`strlen` and `str_eq` operate on bytes, not Unicode scalar values or grapheme
clusters. `parse_i32` accepts an optional leading `-` followed by decimal
digits. Empty strings, invalid characters, and overflow return `0` until Rexc
has richer result types.
```

- [ ] **Step 3: Update README examples**

Add this example after the existing `std_io.rx` command example:

````markdown
```rust
fn main() -> i32 {
    println("number?");
    let value: i32 = read_i32();
    print("double: ");
    println_i32(value + value);
    return 0;
}
```
````

- [ ] **Step 4: Update symbol documentation**

In `README.md`, update the sentence beginning `Source-level prelude names are` so it lists:

```markdown
`print`, `println`, `read_line`, `strlen`, `str_eq`, `print_i32`,
`println_i32`, `parse_i32`, `read_i32`, and `exit`.
```

- [ ] **Step 5: Update the design doc**

In `docs/superpowers/specs/2026-04-25-rexc-layered-standard-library-design.md`, add a new section before `Future Direction`:

```markdown
## Phase Two Expansion

The next standard-library increment adds byte-string helpers and signed i32
console helpers while preserving the prelude-only surface:

| Function | Type | Behavior |
| --- | --- | --- |
| `strlen` | `fn(str) -> i32` | Returns the byte length up to the first null terminator. |
| `str_eq` | `fn(str, str) -> bool` | Returns true when both null-terminated byte strings contain the same bytes. |
| `print_i32` | `fn(i32) -> i32` | Writes a signed decimal integer without a newline. |
| `println_i32` | `fn(i32) -> i32` | Writes a signed decimal integer followed by a newline. |
| `parse_i32` | `fn(str) -> i32` | Parses a signed decimal integer and returns 0 for invalid or overflow input. |
| `read_i32` | `fn() -> i32` | Reads one line from stdin and parses it as i32. |

These functions are intentionally byte-oriented and i32-specific. They make
small command-line programs more useful without adding formatting macros,
generics, heap strings, slices, or result types.
```

- [ ] **Step 6: Run verification**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass after documentation changes.

- [ ] **Step 7: Commit**

Run:

```sh
git add README.md docs/superpowers/specs/2026-04-25-rexc-layered-standard-library-design.md
git commit -m "docs: document std phase two"
```

### Task 11: Final Verification And Follow-Up Notes

**Files:**
- No source changes.

- [ ] **Step 1: Run full verification**

Run:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all configured tests pass.

- [ ] **Step 2: Inspect working tree**

Run:

```sh
git status --short
```

Expected: clean working tree after the commits above, or only intentional user changes unrelated to this plan.

- [ ] **Step 3: Record follow-up candidates**

Use this exact follow-up list in the implementation handoff:

```text
- Add `u32` printing and parsing after signed i32 behavior is stable.
- Add `str_cmp(str, str) -> i32` if ordering becomes useful for examples.
- Add Drunix runtime adapters once the hosted std ABI settles.
- Revisit parse error reporting when Rexc has option/result-like types.
- Move from prelude-only names to `std::...` paths after module syntax exists.
```
