# macOS arm64 Binary and Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a native Apple Silicon `rexc` CLI and add a Rexc backend target that emits Apple ARM64 assembly/object files for macOS.

**Architecture:** Split shared codegen types out of the current x86-specific public header, keep the existing i386/x86_64 backend intact, and add a new Darwin ARM64 backend for the same typed IR. The first ARM64 backend should support the existing scalar/control-flow/pointer feature set, emit Mach-O/Darwin assembly symbols with leading underscores, and assemble object files with Apple `as -arch arm64`.

**Tech Stack:** C++17, CMake 3.20+, Apple Clang, Java/OpenJDK for ANTLR generation, Darwin ARM64 assembly, existing Rexc unit/smoke test harness.

---

### Task 1: Neutral Codegen API

**Files:**
- Create: `include/rexc/codegen.hpp`
- Modify: `include/rexc/codegen_x86.hpp`
- Modify: `src/codegen_x86.cpp`
- Modify: `tests/codegen_tests.cpp`

- [ ] **Step 1: Create shared codegen declarations**

Create `include/rexc/codegen.hpp`:

```cpp
#pragma once

#include <string>

namespace rexc {

class CodegenResult {
public:
	CodegenResult(bool ok, std::string assembly);

	bool ok() const;
	const std::string &assembly() const;

private:
	bool ok_ = false;
	std::string assembly_;
};

enum class CodegenTarget {
	I386,
	X86_64,
	ARM64_MACOS,
};

} // namespace rexc
```

- [ ] **Step 2: Update the x86 header**

Change `include/rexc/codegen_x86.hpp` so it includes the shared header and only declares the x86 emitter:

```cpp
#pragma once

// Public x86 backend API: typed IR in, target assembly or diagnostics out.
#include "rexc/codegen.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/ir.hpp"

namespace rexc {

CodegenResult emit_x86_assembly(const ir::Module &module, Diagnostics &diagnostics,
                                CodegenTarget target = CodegenTarget::I386);

} // namespace rexc
```

- [ ] **Step 3: Keep `CodegenResult` definitions in one source file**

Leave the existing `CodegenResult` constructor/accessor definitions in `src/codegen_x86.cpp` for this task, but include `rexc/codegen.hpp` through `rexc/codegen_x86.hpp`. Do not duplicate those definitions in the ARM64 backend later.

- [ ] **Step 4: Update tests that mention the enum**

Keep `tests/codegen_tests.cpp` including `rexc/codegen_x86.hpp`; no behavioral test change is expected in this task.

- [ ] **Step 5: Verify no x86 regression**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: all unit tests pass.

- [ ] **Step 6: Commit**

Run:

```sh
git add include/rexc/codegen.hpp include/rexc/codegen_x86.hpp src/codegen_x86.cpp tests/codegen_tests.cpp
git commit -m "refactor: share codegen result and target types"
```

### Task 2: macOS arm64 Build Preset

**Files:**
- Create: `CMakePresets.json`

- [ ] **Step 1: Create the preset file**

Create `CMakePresets.json`:

```json
{
  "version": 6,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 20,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "macos-arm64-release",
      "displayName": "macOS arm64 Release",
      "description": "Build the rexc CLI as a native Apple Silicon macOS executable.",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/macos-arm64-release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_OSX_ARCHITECTURES": "arm64"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "macos-arm64-release",
      "configurePreset": "macos-arm64-release"
    }
  ],
  "testPresets": [
    {
      "name": "macos-arm64-release",
      "configurePreset": "macos-arm64-release",
      "output": {
        "outputOnFailure": true
      }
    }
  ]
}
```

- [ ] **Step 2: Configure and build**

Run:

```sh
cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-release
file build/macos-arm64-release/rexc
lipo -archs build/macos-arm64-release/rexc
```

Expected output includes:

```text
Mach-O 64-bit executable arm64
arm64
```

- [ ] **Step 3: Commit**

Run:

```sh
git add CMakePresets.json
git commit -m "build: add macos arm64 release preset"
```

### Task 3: Darwin ARM64 Backend Skeleton

**Files:**
- Create: `include/rexc/codegen_arm64.hpp`
- Create: `src/codegen_arm64.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/codegen_arm64_tests.cpp`

- [ ] **Step 1: Add the ARM64 public header**

Create `include/rexc/codegen_arm64.hpp`:

```cpp
#pragma once

// Public Darwin ARM64 backend API: typed IR in, Mach-O-compatible assembly out.
#include "rexc/codegen.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/ir.hpp"

namespace rexc {

CodegenResult emit_arm64_macos_assembly(const ir::Module &module,
                                        Diagnostics &diagnostics);

} // namespace rexc
```

- [ ] **Step 2: Add a minimal ARM64 emitter**

Create `src/codegen_arm64.cpp` with a minimal function-only skeleton:

```cpp
#include "rexc/codegen_arm64.hpp"

#include <sstream>

namespace rexc {
namespace {

std::string darwin_symbol(const std::string &name)
{
	return "_" + name;
}

class Arm64Emitter {
public:
	CodegenResult emit(const ir::Module &module)
	{
		out_ << ".text\n";
		for (const auto &function : module.functions) {
			if (function.is_extern)
				continue;
			emit_function(function);
		}
		return CodegenResult(true, out_.str());
	}

private:
	void emit_function(const ir::Function &function)
	{
		const std::string symbol = darwin_symbol(function.name);
		out_ << ".globl " << symbol << '\n';
		out_ << ".p2align 2\n";
		out_ << symbol << ":\n";
		out_ << "\tstp x29, x30, [sp, #-16]!\n";
		out_ << "\tmov x29, sp\n";
		out_ << "\tmov w0, #0\n";
		out_ << "\tldp x29, x30, [sp], #16\n";
		out_ << "\tret\n\n";
	}

	std::ostringstream out_;
};

} // namespace

CodegenResult emit_arm64_macos_assembly(const ir::Module &module,
                                        Diagnostics &diagnostics)
{
	(void)diagnostics;
	return Arm64Emitter().emit(module);
}

} // namespace rexc
```

- [ ] **Step 3: Add the source and test to CMake**

Modify `CMakeLists.txt`:

```cmake
add_library(rexc_core
	src/source.cpp
	src/diagnostics.cpp
	src/ast.cpp
	src/parse.cpp
	src/sema.cpp
	src/ir.cpp
	src/lower_ir.cpp
	src/codegen_x86.cpp
	src/codegen_arm64.cpp
	src/types.cpp
	${REXC_ANTLR_GENERATED_SOURCES}
)
```

Add `tests/codegen_arm64_tests.cpp` to `add_executable(rexc_tests ...)`.

- [ ] **Step 4: Add the first ARM64 codegen test**

Create `tests/codegen_arm64_tests.cpp`:

```cpp
#include "rexc/codegen_arm64.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"
#include "test_support.hpp"

#include <string>
#include <utility>

static std::string compile_to_arm64_assembly(const std::string &text)
{
	rexc::SourceFile source("test.rx", text);
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());
	auto result = rexc::emit_arm64_macos_assembly(rexc::lower_to_ir(parsed.module()), diagnostics);
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
```

- [ ] **Step 5: Verify skeleton**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: all tests pass.

- [ ] **Step 6: Commit**

Run:

```sh
git add include/rexc/codegen_arm64.hpp src/codegen_arm64.cpp CMakeLists.txt tests/codegen_arm64_tests.cpp
git commit -m "feat: add darwin arm64 codegen skeleton"
```

### Task 4: ARM64 Values, Stack Frame, and Calls

**Files:**
- Modify: `src/codegen_arm64.cpp`
- Modify: `tests/codegen_arm64_tests.cpp`

- [ ] **Step 1: Add failing tests for scalar output**

Add tests:

```cpp
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
```

- [ ] **Step 2: Verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: ARM64 tests fail because the skeleton returns `0` and does not emit expressions, locals, calls, or strings.

- [ ] **Step 3: Implement ARM64 frame layout**

In `src/codegen_arm64.cpp`, add:

```cpp
using SlotMap = std::unordered_map<std::string, int>;

struct Frame {
	SlotMap parameter_slots;
	std::unordered_map<const ir::LetStatement *, int> let_slots;
	int local_bytes = 0;
};

static int align16(int value)
{
	return (value + 15) & ~15;
}
```

Use 8-byte stack slots for all current IR values. Spill incoming arguments `x0` through `x7` into negative frame offsets; read stack-passed arguments from positive offsets if needed. Reserve `16 + align16(slot_count * 8)` bytes so the stack remains 16-byte aligned after saving `x29/x30`.

- [ ] **Step 4: Implement ARM64 value emission**

Implement value emission with these register conventions:

```text
x0/w0: accumulator and function return value
x1/w1: right-hand side scratch value
x9: address scratch
x29: frame pointer
x30: link register
```

Required instruction patterns:

```asm
mov w0, #42
mov x0, #4000000000
str x0, [x29, #-8]
ldr x0, [x29, #-8]
adrp x0, Lstr0@PAGE
add x0, x0, Lstr0@PAGEOFF
bl _callee
```

- [ ] **Step 5: Implement binary arithmetic**

Emit left operand to `x0`, push it to a temporary stack slot or `sub sp, sp, #16; str x0, [sp]`, emit right operand to `x0`, load left into `x1`, then emit:

```asm
add x0, x1, x0
sub x0, x1, x0
mul x0, x1, x0
sdiv x0, x1, x0
udiv x0, x1, x0
```

Use `w0/w1` for 32-bit scalar operations when possible, but keep stack slots 8 bytes.

- [ ] **Step 6: Verify green**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: all ARM64 scalar/call/string tests pass.

- [ ] **Step 7: Commit**

Run:

```sh
git add src/codegen_arm64.cpp tests/codegen_arm64_tests.cpp
git commit -m "feat: emit arm64 scalar values and calls"
```

### Task 5: ARM64 Control Flow, Casts, and Pointers

**Files:**
- Modify: `src/codegen_arm64.cpp`
- Modify: `tests/codegen_arm64_tests.cpp`

- [ ] **Step 1: Add failing tests for branches and loops**

Add tests:

```cpp
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
```

- [ ] **Step 2: Add failing tests for casts and pointers**

Add tests:

```cpp
TEST_CASE(codegen_arm64_macos_emits_casts)
{
	auto assembly = compile_to_arm64_assembly("fn main() -> i32 { return 'A' as i32; }\n");

	REQUIRE(assembly.find("mov w0, #65") != std::string::npos);
}

TEST_CASE(codegen_arm64_macos_emits_pointer_load_store)
{
	auto assembly = compile_to_arm64_assembly(
		"fn main() -> i32 { let mut x: i32 = 1; let p: *i32 = &x; *p = 2; return *p; }\n");

	REQUIRE(assembly.find("add x0, x29, #-") != std::string::npos);
	REQUIRE(assembly.find("str x1, [x0]") != std::string::npos);
	REQUIRE(assembly.find("ldr x0, [x0]") != std::string::npos);
}
```

- [ ] **Step 3: Verify red**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: ARM64 tests fail until control flow, casts, address-of, dereference, pointer indexing, and indirect assignment are implemented.

- [ ] **Step 4: Implement ARM64 branches**

Use these patterns:

```asm
cmp x1, x0
cset w0, eq
cset w0, ne
cset w0, lt
cset w0, le
cset w0, gt
cset w0, ge
cbz w0, L_else_0
b L_end_if_1
```

For unsigned comparisons, use condition codes `lo`, `ls`, `hi`, and `hs`.

- [ ] **Step 5: Implement logical short-circuiting**

Emit `&&` and `||` with labels:

```asm
cbz w0, L_logic_false_0
cbnz w0, L_logic_true_1
mov w0, #0
mov w0, #1
```

Preserve existing Rexc semantics: right-hand expressions are not evaluated when the left-hand operand decides the result.

- [ ] **Step 6: Implement casts and pointer operations**

Use:

```asm
sxtb w0, w0
sxth w0, w0
uxtb w0, w0
uxth w0, w0
add x0, x29, #-8
ldr x0, [x0]
str x1, [x0]
lsl x1, x1, #2
add x0, x0, x1
```

Scale pointer arithmetic by the pointee size from `PrimitiveType`.

- [ ] **Step 7: Verify green**

Run:

```sh
cmake --build build
build/rexc_tests
```

Expected: all ARM64 backend tests pass.

- [ ] **Step 8: Commit**

Run:

```sh
git add src/codegen_arm64.cpp tests/codegen_arm64_tests.cpp
git commit -m "feat: emit arm64 control flow and pointers"
```

### Task 6: CLI Target Parsing and Object Assembly

**Files:**
- Modify: `src/main.cpp`
- Modify: `tests/cli_smoke.sh`
- Modify: `tests/assemble_smoke.sh`

- [ ] **Step 1: Add CLI smoke expectations**

Add to `tests/cli_smoke.sh`:

```sh
"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -S -o "${tmp_dir}/add-arm64.s"
test -s "${tmp_dir}/add-arm64.s"
grep -F -q '.globl _main' "${tmp_dir}/add-arm64.s"
grep -F -q 'bl _add' "${tmp_dir}/add-arm64.s"
grep -F -q 'ret' "${tmp_dir}/add-arm64.s"
```

- [ ] **Step 2: Update CLI target parsing**

In `src/main.cpp`, include the ARM64 backend:

```cpp
#include "rexc/codegen_arm64.hpp"
```

Update `parse_target`:

```cpp
if (target == "arm64-macos" || target == "aarch64-apple-darwin")
	return rexc::CodegenTarget::ARM64_MACOS;
```

Update the usage text:

```cpp
"usage: rexc input.rx [--target i386|x86_64|arm64-macos] "
```

- [ ] **Step 3: Dispatch to the right backend**

Replace the direct x86 call in `main`:

```cpp
rexc::CodegenResult codegen =
	options.target == rexc::CodegenTarget::ARM64_MACOS
		? rexc::emit_arm64_macos_assembly(ir, diagnostics)
		: rexc::emit_x86_assembly(ir, diagnostics, options.target);
```

- [ ] **Step 4: Assemble ARM64 macOS objects**

Update `assemble_object`:

```cpp
if (target == rexc::CodegenTarget::ARM64_MACOS) {
	if (std::system("command -v as >/dev/null 2>&1") != 0)
		throw std::runtime_error("no Apple assembler found");
	std::string command = "as -arch arm64 -o " + shell_quote(object_path) +
	                      " " + shell_quote(assembly_path);
	run_tool(command, "assembler failed");
	return;
}
```

Keep the current GNU assembler path for i386 and x86_64.

- [ ] **Step 5: Add assemble smoke for Darwin ARM64**

Add to `tests/assemble_smoke.sh`:

```sh
"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -S -o "${tmp_dir}/add-arm64.s"

if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
	as -arch arm64 -o "${tmp_dir}/add-arm64.o" "${tmp_dir}/add-arm64.s"
	"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -c -o "${tmp_dir}/add-arm64-cli.o"
	test -s "${tmp_dir}/add-arm64.o"
	test -s "${tmp_dir}/add-arm64-cli.o"
else
	echo "SKIP: arm64-macos object smoke requires Apple Silicon macOS"
fi
```

- [ ] **Step 6: Guard GNU assembler smoke on macOS**

Before the existing i386/x86_64 object assembly block, select only a GNU-compatible assembler:

```sh
gnu_as=
if command -v x86_64-elf-as >/dev/null 2>&1; then
	gnu_as=x86_64-elf-as
elif command -v as >/dev/null 2>&1 && as --version 2>/dev/null | grep -qi 'gnu assembler'; then
	gnu_as=as
fi

if [ -z "$gnu_as" ]; then
	echo "SKIP: no GNU assembler found"
else
	"$gnu_as" --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
	"$gnu_as" --32 -o "${tmp_dir}/branch32.o" "${tmp_dir}/branch32.s"
	"$gnu_as" --64 -o "${tmp_dir}/wide64.o" "${tmp_dir}/wide64.s"
	"$gnu_as" --64 -o "${tmp_dir}/branch64.o" "${tmp_dir}/branch64.s"
	test -s "${tmp_dir}/add.o"
	test -s "${tmp_dir}/branch32.o"
	test -s "${tmp_dir}/wide64.o"
	test -s "${tmp_dir}/branch64.o"
fi
```

- [ ] **Step 7: Verify CLI and object output**

Run:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
build/rexc examples/add.rx --target arm64-macos -S -o build/add-arm64.s
build/rexc examples/add.rx --target arm64-macos -c -o build/add-arm64.o
file build/add-arm64.o
```

Expected on Apple Silicon macOS:

```text
build/add-arm64.o: Mach-O 64-bit object arm64
```

- [ ] **Step 8: Commit**

Run:

```sh
git add src/main.cpp tests/cli_smoke.sh tests/assemble_smoke.sh
git commit -m "feat: expose arm64 macos target"
```

### Task 7: Packaging Script and Make Targets

**Files:**
- Create: `scripts/package_macos_arm64.sh`
- Modify: `Makefile`

- [ ] **Step 1: Add packaging script**

Create `scripts/package_macos_arm64.sh`:

```sh
#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="${repo_dir}/build/macos-arm64-release"
dist_dir="${repo_dir}/dist"
package_dir="${dist_dir}/rexc-macos-arm64"
archive="${dist_dir}/rexc-macos-arm64.tar.gz"
checksum="${archive}.sha256"

if [ "$(uname -s)" != "Darwin" ] || [ "$(uname -m)" != "arm64" ]; then
	echo "error: macOS arm64 packaging must run from a native Apple Silicon shell" >&2
	exit 1
fi

cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-release
ctest --preset macos-arm64-release --output-on-failure

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -S -o "${build_dir}/add-arm64.s"
"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -c -o "${build_dir}/add-arm64.o"
file "${build_dir}/rexc" | grep -q 'Mach-O 64-bit executable arm64'
file "${build_dir}/add-arm64.o" | grep -q 'Mach-O 64-bit object arm64'

rm -rf "$package_dir" "$archive" "$checksum"
mkdir -p "$package_dir"
cp "${build_dir}/rexc" "$package_dir/rexc"
chmod 755 "$package_dir/rexc"

cat > "$package_dir/README.txt" <<'TXT'
Rexc macOS arm64 package

This package contains a native Apple Silicon rexc CLI.

Examples:
  ./rexc input.rx -S -o output-x86.s
  ./rexc input.rx --target arm64-macos -S -o output-arm64.s
  ./rexc input.rx --target arm64-macos -c -o output-arm64.o
TXT

(cd "$dist_dir" && tar -czf "$(basename "$archive")" rexc-macos-arm64)
shasum -a 256 "$archive" > "$checksum"

echo "Wrote $archive"
echo "Wrote $checksum"
```

- [ ] **Step 2: Make script executable**

Run:

```sh
chmod +x scripts/package_macos_arm64.sh
```

- [ ] **Step 3: Add Makefile targets**

Change the `.PHONY` line:

```make
.PHONY: build test build-macos-arm64 test-macos-arm64 package-macos-arm64
```

Add:

```make
build-macos-arm64:
	cmake --preset macos-arm64-release
	cmake --build --preset macos-arm64-release

test-macos-arm64: build-macos-arm64
	ctest --preset macos-arm64-release --output-on-failure

package-macos-arm64:
	./scripts/package_macos_arm64.sh
```

- [ ] **Step 4: Verify package**

Run:

```sh
make package-macos-arm64
tar -tzf dist/rexc-macos-arm64.tar.gz
```

Expected archive contents:

```text
rexc-macos-arm64/
rexc-macos-arm64/rexc
rexc-macos-arm64/README.txt
```

- [ ] **Step 5: Commit**

Run:

```sh
git add scripts/package_macos_arm64.sh Makefile
git commit -m "build: package macos arm64 compiler"
```

### Task 8: Documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update target table**

Add this row to the Targets table:

```markdown
| `arm64-macos` | `--target arm64-macos` | Apple `as -arch arm64` | Mach-O 64-bit arm64 object | Uses Darwin symbol names and Apple ARM64 calling convention. |
```

- [ ] **Step 2: Add build/package docs**

Add after the existing Build section:

```markdown
## Build A macOS arm64 Compiler

On Apple Silicon macOS, build and verify a native `arm64` `rexc` executable:

```sh
cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-release
ctest --preset macos-arm64-release --output-on-failure
file build/macos-arm64-release/rexc
lipo -archs build/macos-arm64-release/rexc
```

Package it with:

```sh
make package-macos-arm64
```

The archive and checksum are written to:

```text
dist/rexc-macos-arm64.tar.gz
dist/rexc-macos-arm64.tar.gz.sha256
```
```

- [ ] **Step 3: Add ARM64 compile examples**

Add to the Compile, Assemble, And Link section:

```markdown
```sh
build/rexc examples/add.rx --target arm64-macos -S -o build/add-arm64.s
build/rexc examples/add.rx --target arm64-macos -c -o build/add-arm64.o
file build/add-arm64.o
```
```

- [ ] **Step 4: Commit**

Run:

```sh
git add README.md
git commit -m "docs: document macos arm64 compiler and target"
```

### Task 9: Final Verification

**Files:**
- Verify: all changed files

- [ ] **Step 1: Run default verification**

Run:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass on the default local build.

- [ ] **Step 2: Run macOS arm64 verification**

On Apple Silicon macOS, run:

```sh
make build-macos-arm64
make test-macos-arm64
build/macos-arm64-release/rexc examples/add.rx --target arm64-macos -S -o build/macos-arm64-release/add-arm64.s
build/macos-arm64-release/rexc examples/add.rx --target arm64-macos -c -o build/macos-arm64-release/add-arm64.o
file build/macos-arm64-release/rexc
file build/macos-arm64-release/add-arm64.o
```

Expected:

```text
Mach-O 64-bit executable arm64
Mach-O 64-bit object arm64
```

- [ ] **Step 3: Run package verification**

Run:

```sh
make package-macos-arm64
tar -xzf dist/rexc-macos-arm64.tar.gz -C /tmp
/tmp/rexc-macos-arm64/rexc examples/add.rx --target arm64-macos -c -o /tmp/rexc-add-arm64.o
file /tmp/rexc-add-arm64.o
```

Expected:

```text
Mach-O 64-bit object arm64
```

- [ ] **Step 4: Run syntax and whitespace checks**

Run:

```sh
sh -n scripts/package_macos_arm64.sh
sh -n tests/cli_smoke.sh
sh -n tests/assemble_smoke.sh
git diff --check
```

Expected: no syntax errors and no whitespace errors.

### Follow-Up: Linked macOS Executables

This plan stops at `-S` assembly and `-c` Mach-O object output for `arm64-macos`. Producing a linked runnable macOS executable should be a separate plan because it requires deciding runtime/startup behavior, libc/system call boundaries, linker invocation, code signing expectations, and whether Rexc programs expose `_main` for C runtime startup or a custom `_start`.
