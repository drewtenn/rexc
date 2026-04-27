# Default Prelude Policy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decide and implement Rexy's default prelude policy by narrowing which stdlib functions are available as bare names while preserving runtime emission and explicit stdlib paths.

**Architecture:** Split stdlib declarations into three uses: all runtime declarations, default bare prelude declarations, and disabled stdlib declarations for compiling the stdlib itself. Semantic analysis and IR lowering should use matching policy enums so accepted source and lowered symbols cannot diverge. This slice does not build ordinary stdlib module loading; explicit `std::...` bridge paths keep working where `std_*` symbols already provide them.

**Tech Stack:** C++17, existing Rexy AST/sema/lower/codegen pipeline, embedded `.rx` stdlib source, Catch2-style `rexc_tests`.

---

## Final Policy For This Slice

The default bare prelude contains user-facing conveniences only:

```text
print, println, read_line,
strlen, str_eq, str_is_empty, str_starts_with, str_ends_with, str_contains, str_find,
parse_i32, parse_bool,
print_i32, println_i32, print_bool, println_bool, print_char, println_char,
read_i32, read_bool,
panic
```

These names are intentionally not in the default bare prelude:

```text
memset_u8, memcpy_u8, str_copy_to,
slice_u8_len, slice_u8_is_empty, slice_u8_get_or,
result_is_ok, result_is_err, error_out_of_memory,
alloc_bytes, alloc_used, alloc_can_allocate, alloc_str_copy, alloc_str_concat,
owned_str_clone, box_i32_new, vec_i32_new, vec_i32_push,
alloc_i32_to_str, alloc_bool_to_str, alloc_char_to_str, alloc_remaining, alloc_reset,
exit, abort,
std_io_println, std_io_read_line, std_process_exit, std_env_get,
std_fs_open_read, std_fs_create_write, std_path_join,
args_len, arg_at, env_get, file_open_read, file_create_write, file_close,
file_read, file_write_str, path_join
```

Rationale:

- Keep simple teaching examples working without imports.
- Stop treating bootstrap allocation, memory, sentinel-result, file, env, path, and bridge names as always-available language surface.
- Keep all stdlib functions emitted into hosted runtimes so default prelude narrowing does not affect linking.
- Keep explicit qualified bridge paths such as `std::io::println` and `std::process::exit` working.

## Files

- Modify: `include/rexc/stdlib.hpp`
  - Add a declaration query for all stdlib functions.
  - Keep `prelude_functions()` as the default bare prelude.
- Modify: `src/stdlib/stdlib.cpp`
  - Build all stdlib function declarations once.
  - Filter default prelude declarations through a fixed allowlist.
  - Make `find_prelude_function()` search the default prelude only.
  - Add `find_stdlib_function()` for all declarations.
- Modify: `include/rexc/sema.hpp`
  - Replace the boolean prelude option with an explicit `StdlibSymbolPolicy`.
- Modify: `include/rexc/lower_ir.hpp`
  - Mirror the sema choices with a distinct `LowerStdlibSymbolPolicy` for lowering.
- Modify: `src/sema.cpp`
  - Register bare stdlib names according to policy.
  - Register explicit `std::...` bridge paths for bridge-backed `std_*`
    declarations when policy is not `None`.
- Modify: `src/lower_ir.cpp`
  - Apply the same policy as sema so lowered calls match accepted calls.
- Modify: `tests/stdlib_tests.cpp`
  - Split tests between all stdlib declarations and default prelude declarations.
- Modify: `tests/sema_tests.cpp`
  - Assert default prelude accepts intended names and rejects omitted names.
- Modify: `tests/codegen_tests.cpp`
  - Keep backend coverage for non-default bootstrap helpers by compiling those snippets with `StdlibSymbolPolicy::All`.
- Modify: `tests/codegen_arm64_tests.cpp`
  - Same backend coverage adjustment for ARM64 helper snippets.
- Modify: `tests/ir_tests.cpp`
  - Update any low-level stdlib lowering tests to opt into all stdlib symbols.
- Modify: `docs/ch03-semantics.md`
  - Document the default prelude policy.
- Modify: `docs/roadmap.md`
  - Mark this Phase 8 roadmap item done for the current module model.

## Task 1: Add Stdlib Declaration Views

**Files:**
- Modify: `include/rexc/stdlib.hpp`
- Modify: `src/stdlib/stdlib.cpp`
- Test: `tests/stdlib_tests.cpp`

- [x] **Step 1: Write the failing stdlib declaration tests**

In `tests/stdlib_tests.cpp`, rename `TEST_CASE(stdlib_declares_prelude_functions)` to:

```cpp
TEST_CASE(stdlib_declares_all_public_functions)
```

Inside that test, replace every call to `rexc::stdlib::find_prelude_function(...)` with `rexc::stdlib::find_stdlib_function(...)`.

After that test, add this new test:

```cpp
TEST_CASE(stdlib_default_prelude_contains_only_user_facing_names)
{
	auto println = rexc::stdlib::find_prelude_function("println");
	auto read_line = rexc::stdlib::find_prelude_function("read_line");
	auto str_eq = rexc::stdlib::find_prelude_function("str_eq");
	auto parse_i32 = rexc::stdlib::find_prelude_function("parse_i32");
	auto print_i32 = rexc::stdlib::find_prelude_function("print_i32");
	auto panic = rexc::stdlib::find_prelude_function("panic");

	REQUIRE(println != nullptr);
	REQUIRE(read_line != nullptr);
	REQUIRE(str_eq != nullptr);
	REQUIRE(parse_i32 != nullptr);
	REQUIRE(print_i32 != nullptr);
	REQUIRE(panic != nullptr);

	REQUIRE(rexc::stdlib::find_prelude_function("memset_u8") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("alloc_bytes") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("alloc_i32_to_str") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("result_is_ok") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("file_open_read") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("path_join") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("std_io_println") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("exit") == nullptr);
	REQUIRE(rexc::stdlib::find_prelude_function("abort") == nullptr);
}
```

- [x] **Step 2: Run the tests and verify they fail**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "stdlib*"
```

Expected: compile failure because `find_stdlib_function` is not declared, or test failure because the current prelude still includes omitted names.

- [x] **Step 3: Add the public declarations**

In `include/rexc/stdlib.hpp`, replace:

```cpp
const std::vector<FunctionDecl> &prelude_functions();
const FunctionDecl *find_prelude_function(const std::string &name);
std::string hosted_runtime_assembly(TargetTriple target);
```

with:

```cpp
const std::vector<FunctionDecl> &stdlib_functions();
const std::vector<FunctionDecl> &prelude_functions();
const FunctionDecl *find_stdlib_function(const std::string &name);
const FunctionDecl *find_prelude_function(const std::string &name);
std::string hosted_runtime_assembly(TargetTriple target);
```

- [x] **Step 4: Implement all-functions and filtered-prelude views**

In `src/stdlib/stdlib.cpp`, add this include:

```cpp
#include <unordered_set>
```

Add this helper inside the anonymous namespace:

```cpp
const std::unordered_set<std::string> &default_prelude_names()
{
	static const std::unordered_set<std::string> names{
	    "print", "println", "read_line",
	    "strlen", "str_eq", "str_is_empty", "str_starts_with", "str_ends_with",
	    "str_contains", "str_find",
	    "parse_i32", "parse_bool",
	    "print_i32", "println_i32", "print_bool", "println_bool", "print_char",
	    "println_char", "read_i32", "read_bool",
	    "panic"};
	return names;
}

bool is_default_prelude_function(const FunctionDecl &function)
{
	return default_prelude_names().find(function.name) != default_prelude_names().end();
}

const FunctionDecl *find_function_in(const std::vector<FunctionDecl> &functions,
                                     const std::string &name)
{
	for (const auto &function : functions) {
		if (function.name == name)
			return &function;
		if (auto path = stdlib_path_for_symbol(function.name);
		    path && canonical_path(*path) == name)
			return &function;
	}
	return nullptr;
}
```

Replace the existing `prelude_functions()` and `find_prelude_function()` implementations with:

```cpp
const std::vector<FunctionDecl> &stdlib_functions()
{
	static const std::vector<FunctionDecl> functions = [] {
		std::vector<FunctionDecl> result;
		for (const auto &unit : portable_stdlib_source_units())
			append_source_unit_declarations(result, unit);
		return result;
	}();
	return functions;
}

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions = [] {
		std::vector<FunctionDecl> result;
		for (const auto &function : stdlib_functions()) {
			if (is_default_prelude_function(function))
				result.push_back(function);
		}
		return result;
	}();
	return functions;
}

const FunctionDecl *find_stdlib_function(const std::string &name)
{
	return find_function_in(stdlib_functions(), name);
}

const FunctionDecl *find_prelude_function(const std::string &name)
{
	return find_function_in(prelude_functions(), name);
}
```

- [x] **Step 5: Run stdlib tests and verify they pass**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "stdlib*"
```

Expected: `stdlib_declares_all_public_functions`, `stdlib_default_prelude_contains_only_user_facing_names`, and existing stdlib runtime tests pass.

- [x] **Step 6: Commit the stdlib declaration split**

```bash
git add include/rexc/stdlib.hpp src/stdlib/stdlib.cpp tests/stdlib_tests.cpp
git commit -m "feat: split stdlib declarations from default prelude"
```

## Task 2: Add A Shared Stdlib Symbol Policy

**Files:**
- Modify: `include/rexc/sema.hpp`
- Modify: `include/rexc/lower_ir.hpp`
- Modify: `src/stdlib/stdlib.cpp`
- Modify: `src/sema.cpp`
- Modify: `src/lower_ir.cpp`
- Test: `tests/sema_tests.cpp`

- [x] **Step 1: Write failing sema tests for default and explicit policies**

In `tests/sema_tests.cpp`, after `sema_accepts_std_prelude_panic`, add:

```cpp
TEST_CASE(sema_rejects_non_default_stdlib_helper_as_bare_name)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "fn main() -> i32 { alloc_reset(); return 0; }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown function 'alloc_reset'") !=
	        std::string::npos);
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
```

- [x] **Step 2: Run sema tests and verify they fail**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
```

Expected: `sema_rejects_non_default_stdlib_helper_as_bare_name` and `sema_rejects_std_bridge_symbol_as_bare_name` fail because all public stdlib functions are still bare names.

- [x] **Step 3: Add the policy enum to sema options**

In `include/rexc/sema.hpp`, replace:

```cpp
struct SemanticOptions {
	bool include_stdlib_prelude = true;
};
```

with:

```cpp
enum class StdlibSymbolPolicy {
	None,
	DefaultPrelude,
	All,
};

struct SemanticOptions {
	StdlibSymbolPolicy stdlib_symbols = StdlibSymbolPolicy::DefaultPrelude;
};
```

- [x] **Step 4: Add the matching policy enum to lowering options**

In `include/rexc/lower_ir.hpp`, replace:

```cpp
struct LowerOptions {
	bool include_stdlib_prelude = true;
};
```

with:

```cpp
enum class LowerStdlibSymbolPolicy {
	None,
	DefaultPrelude,
	All,
};

struct LowerOptions {
	LowerStdlibSymbolPolicy stdlib_symbols = LowerStdlibSymbolPolicy::DefaultPrelude;
};
```

Use a distinct enum name in `lower_ir.hpp` to avoid accidental include-order coupling between sema and lowering.

- [x] **Step 5: Update stdlib runtime compilation options**

In `src/stdlib/stdlib.cpp`, replace:

```cpp
SemanticOptions semantic_options;
semantic_options.include_stdlib_prelude = false;
auto sema = analyze_module(parsed.module(), diagnostics, semantic_options);
```

with:

```cpp
SemanticOptions semantic_options;
semantic_options.stdlib_symbols = StdlibSymbolPolicy::None;
auto sema = analyze_module(parsed.module(), diagnostics, semantic_options);
```

Replace:

```cpp
LowerOptions lower_options;
lower_options.include_stdlib_prelude = false;
auto lowered = lower_to_ir(parsed.module(), lower_options);
```

with:

```cpp
LowerOptions lower_options;
lower_options.stdlib_symbols = LowerStdlibSymbolPolicy::None;
auto lowered = lower_to_ir(parsed.module(), lower_options);
```

- [x] **Step 6: Implement policy selection in sema**

In `src/sema.cpp`, add these private helpers near `build_module_table()`:

```cpp
const std::vector<stdlib::FunctionDecl> &bare_stdlib_functions() const
{
	if (options_.stdlib_symbols == StdlibSymbolPolicy::All)
		return stdlib::stdlib_functions();
	return stdlib::prelude_functions();
}

bool include_stdlib_symbols() const
{
	return options_.stdlib_symbols != StdlibSymbolPolicy::None;
}

bool include_bare_stdlib_symbols() const
{
	return options_.stdlib_symbols != StdlibSymbolPolicy::None;
}
```

In `build_module_table()`, replace:

```cpp
if (options_.include_stdlib_prelude) {
	for (const auto &function : stdlib::prelude_functions()) {
		if (auto path = stdlib_path_for_symbol(function.name))
			note_item_path(*path, true);
	}
}
```

with:

```cpp
if (include_stdlib_symbols()) {
	for (const auto &function : stdlib::stdlib_functions()) {
		if (auto path = stdlib_path_for_symbol(function.name))
			note_item_path(*path, true);
	}
}
```

In `build_function_table()`, replace the stdlib block with:

```cpp
if (include_stdlib_symbols()) {
	for (const auto &function : stdlib::stdlib_functions()) {
		FunctionInfo info{SourceLocation{}, function.return_type, function.parameters};
		info.visibility = ast::Visibility::Public;
		if (auto path = stdlib_path_for_symbol(function.name)) {
			info.module_path = std::vector<std::string>(path->begin(), path->end() - 1);
			functions_[canonical_path(*path)] = info;
		}
	}
	if (include_bare_stdlib_symbols()) {
		for (const auto &function : bare_stdlib_functions()) {
			FunctionInfo info{SourceLocation{}, function.return_type, function.parameters};
			info.visibility = ast::Visibility::Public;
			functions_[function.name] = std::move(info);
		}
	}
}
```

- [x] **Step 7: Implement policy selection in lowering**

In `src/lower_ir.cpp`, add these private helpers near `build_function_table()`:

```cpp
const std::vector<stdlib::FunctionDecl> &bare_stdlib_functions() const
{
	if (options_.stdlib_symbols == LowerStdlibSymbolPolicy::All)
		return stdlib::stdlib_functions();
	return stdlib::prelude_functions();
}

bool include_stdlib_symbols() const
{
	return options_.stdlib_symbols != LowerStdlibSymbolPolicy::None;
}

bool include_bare_stdlib_symbols() const
{
	return options_.stdlib_symbols != LowerStdlibSymbolPolicy::None;
}
```

In `build_function_table()`, replace the stdlib block with:

```cpp
if (include_stdlib_symbols()) {
	for (const auto &function : stdlib::stdlib_functions()) {
		if (auto path = stdlib_path_for_symbol(function.name)) {
			note_item_path(*path);
			FunctionInfo path_info;
			path_info.return_type = function.return_type;
			path_info.parameter_types = function.parameters;
			path_info.symbol_name = function.name;
			functions_[canonical_path(*path)] = std::move(path_info);
		}
	}
	if (include_bare_stdlib_symbols()) {
		for (const auto &function : bare_stdlib_functions()) {
			FunctionInfo info;
			info.return_type = function.return_type;
			info.parameter_types = function.parameters;
			info.symbol_name = function.name;
			functions_[function.name] = std::move(info);
		}
	}
}
```

- [x] **Step 8: Run sema tests and verify the policy passes**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
```

Expected: default-prelude tests pass. Tests that directly call omitted helpers may now fail and will be handled in the next task.

- [x] **Step 9: Commit policy plumbing**

```bash
git add include/rexc/sema.hpp include/rexc/lower_ir.hpp src/stdlib/stdlib.cpp src/sema.cpp src/lower_ir.cpp tests/sema_tests.cpp
git commit -m "feat: add default stdlib symbol policy"
```

## Task 3: Update Backend Tests That Intentionally Cover Bootstrap Helpers

**Files:**
- Modify: `tests/codegen_tests.cpp`
- Modify: `tests/codegen_arm64_tests.cpp`
- Modify: `tests/ir_tests.cpp`

- [x] **Step 1: Add all-stdlib helper options in x86 codegen tests**

In `tests/codegen_tests.cpp`, update the local helper that semantically analyzes snippets. If it currently does:

```cpp
auto sema = rexc::analyze_module(parsed.module(), diagnostics);
```

replace it with:

```cpp
rexc::SemanticOptions semantic_options;
semantic_options.stdlib_symbols = rexc::StdlibSymbolPolicy::DefaultPrelude;
auto sema = rexc::analyze_module(parsed.module(), diagnostics, semantic_options);
```

Add this helper next to the existing `compile_to_assembly(...)` helper:

```cpp
static std::string compile_to_assembly_with_all_stdlib_symbols(
	const std::string &text, rexc::CodegenTarget target = rexc::CodegenTarget::I386)
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
	auto emitted = rexc::emit_x86_assembly(ir, diagnostics, target);
	REQUIRE(emitted.ok());
	return emitted.assembly();
}
```

In `TEST_CASE(codegen_i386_emits_core_memory_helper_calls)`, replace:

```cpp
auto assembly = compile_to_assembly(
    "static mut A: [u8; 8];\n"
    "static mut B: [u8; 8];\n"
    "fn main() -> i32 { return memset_u8(A + 0, 120 as u8, 4) + memcpy_u8(B + 0, A + 0, 4) + str_copy_to(B + 0, \"hello\", 16); }\n");
```

with:

```cpp
auto assembly = compile_to_assembly_with_all_stdlib_symbols(
    "static mut A: [u8; 8];\n"
    "static mut B: [u8; 8];\n"
    "fn main() -> i32 { return memset_u8(A + 0, 120 as u8, 4) + memcpy_u8(B + 0, A + 0, 4) + str_copy_to(B + 0, \"hello\", 16); }\n");
```

In `TEST_CASE(codegen_i386_emits_alloc_helper_calls)`, replace:

```cpp
auto assembly = compile_to_assembly(
    "fn main() -> i32 { alloc_reset(); let p: *u8 = alloc_bytes(8); memset_u8(p, 65 as u8, 8); let copied: str = alloc_str_copy(\"hello\"); let joined: str = alloc_str_concat(copied, \"!\"); let number: str = alloc_i32_to_str(-42); let truth: str = alloc_bool_to_str(true); let letter: str = alloc_char_to_str('z'); if str_eq(joined, \"hello!\") && str_eq(number, \"-42\") && str_eq(truth, \"true\") && str_eq(letter, \"z\") { return alloc_remaining(); } return 0; }\n");
```

with:

```cpp
auto assembly = compile_to_assembly_with_all_stdlib_symbols(
    "fn main() -> i32 { alloc_reset(); let p: *u8 = alloc_bytes(8); memset_u8(p, 65 as u8, 8); let copied: str = alloc_str_copy(\"hello\"); let joined: str = alloc_str_concat(copied, \"!\"); let number: str = alloc_i32_to_str(-42); let truth: str = alloc_bool_to_str(true); let letter: str = alloc_char_to_str('z'); if str_eq(joined, \"hello!\") && str_eq(number, \"-42\") && str_eq(truth, \"true\") && str_eq(letter, \"z\") { return alloc_remaining(); } return 0; }\n");
```

- [x] **Step 2: Add all-stdlib helper options in ARM64 codegen tests**

In `tests/codegen_arm64_tests.cpp`, add this helper next to the existing `compile_to_assembly(...)` helper:

```cpp
static std::string compile_to_assembly_with_all_stdlib_symbols(
	const std::string &text)
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
	auto emitted = rexc::emit_arm64_macos_assembly(ir, diagnostics);
	REQUIRE(emitted.ok());
	return emitted.assembly();
}
```

In `TEST_CASE(codegen_arm64_macos_emits_core_memory_helper_calls)`, replace:

```cpp
auto assembly = compile_to_assembly(
    "static mut A: [u8; 8];\n"
    "static mut B: [u8; 8];\n"
    "fn main() -> i32 { return memset_u8(A + 0, 120 as u8, 4) + memcpy_u8(B + 0, A + 0, 4) + str_copy_to(B + 0, \"hello\", 16); }\n");
```

with:

```cpp
auto assembly = compile_to_assembly_with_all_stdlib_symbols(
    "static mut A: [u8; 8];\n"
    "static mut B: [u8; 8];\n"
    "fn main() -> i32 { return memset_u8(A + 0, 120 as u8, 4) + memcpy_u8(B + 0, A + 0, 4) + str_copy_to(B + 0, \"hello\", 16); }\n");
```

In `TEST_CASE(codegen_arm64_macos_emits_alloc_helper_calls)`, replace:

```cpp
auto assembly = compile_to_assembly(
    "fn main() -> i32 { alloc_reset(); let p: *u8 = alloc_bytes(8); memset_u8(p, 65 as u8, 8); let copied: str = alloc_str_copy(\"hello\"); let joined: str = alloc_str_concat(copied, \"!\"); let number: str = alloc_i32_to_str(-42); let truth: str = alloc_bool_to_str(true); let letter: str = alloc_char_to_str('z'); if str_eq(joined, \"hello!\") && str_eq(number, \"-42\") && str_eq(truth, \"true\") && str_eq(letter, \"z\") { return alloc_remaining(); } return 0; }\n");
```

with:

```cpp
auto assembly = compile_to_assembly_with_all_stdlib_symbols(
    "fn main() -> i32 { alloc_reset(); let p: *u8 = alloc_bytes(8); memset_u8(p, 65 as u8, 8); let copied: str = alloc_str_copy(\"hello\"); let joined: str = alloc_str_concat(copied, \"!\"); let number: str = alloc_i32_to_str(-42); let truth: str = alloc_bool_to_str(true); let letter: str = alloc_char_to_str('z'); if str_eq(joined, \"hello!\") && str_eq(number, \"-42\") && str_eq(truth, \"true\") && str_eq(letter, \"z\") { return alloc_remaining(); } return 0; }\n");
```

- [x] **Step 3: Update IR tests that intentionally lower omitted helpers**

In `tests/ir_tests.cpp`, for tests that lower direct calls to omitted helpers, set both options explicitly:

```cpp
rexc::SemanticOptions semantic_options;
semantic_options.stdlib_symbols = rexc::StdlibSymbolPolicy::All;
auto sema = rexc::analyze_module(parsed.module(), diagnostics, semantic_options);
REQUIRE(sema.ok());

rexc::LowerOptions lower_options;
lower_options.stdlib_symbols = rexc::LowerStdlibSymbolPolicy::All;
auto ir = rexc::lower_to_ir(parsed.module(), lower_options);
```

Leave tests that use default-prelude names such as `println`, `str_eq`, `parse_i32`, and `panic` on default options.

- [x] **Step 4: Run targeted backend tests**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "codegen*"
build/rexc_tests "lowering*"
```

Expected: backend and lowering tests pass with default-prelude snippets using default options and bootstrap-helper snippets using `All`.

- [x] **Step 5: Commit backend test updates**

```bash
git add tests/codegen_tests.cpp tests/codegen_arm64_tests.cpp tests/ir_tests.cpp
git commit -m "test: opt backend helper coverage into all stdlib symbols"
```

## Task 4: Tighten User-Facing Sema Coverage

**Files:**
- Modify: `tests/sema_tests.cpp`

- [x] **Step 1: Rename existing default-prelude test names**

In `tests/sema_tests.cpp`, rename:

```cpp
TEST_CASE(sema_accepts_std_prelude_print_functions)
```

to:

```cpp
TEST_CASE(sema_accepts_default_prelude_print_functions)
```

Rename the other `sema_accepts_std_prelude_*` tests that still use default-prelude names to `sema_accepts_default_prelude_*`.

- [x] **Step 2: Update omitted-helper sema tests**

For tests that currently assert bare `alloc_*`, `memset_u8`, or low-level result helpers are accepted by sema, change them into rejection tests. For example, replace:

```cpp
TEST_CASE(sema_accepts_alloc_helpers)
```

with:

```cpp
TEST_CASE(sema_rejects_alloc_helpers_as_default_bare_names)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "fn main() -> i32 { alloc_reset(); return alloc_remaining(); }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	auto formatted = diagnostics.format();
	REQUIRE(formatted.find("unknown function 'alloc_reset'") != std::string::npos);
	REQUIRE(formatted.find("unknown function 'alloc_remaining'") != std::string::npos);
}
```

Replace:

```cpp
TEST_CASE(sema_accepts_core_memory_helpers)
```

with:

```cpp
TEST_CASE(sema_rejects_memory_helpers_as_default_bare_names)
{
	rexc::Diagnostics diagnostics;

	auto result = analyze(
	    "static mut BUFFER: [u8; 4];\n"
	    "fn main() -> i32 { memset_u8(BUFFER + 0, 0 as u8, 4); return 0; }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown function 'memset_u8'") !=
	        std::string::npos);
}
```

- [x] **Step 3: Add all-policy sema test for compiler-internal coverage**

Add this test near the rejected helper tests:

```cpp
TEST_CASE(sema_all_stdlib_policy_accepts_bootstrap_helpers)
{
	rexc::Diagnostics diagnostics;
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { alloc_reset(); return alloc_remaining(); }\n");
	auto parsed = rexc::parse_source(source, diagnostics);
	REQUIRE(parsed.ok());

	rexc::SemanticOptions options;
	options.stdlib_symbols = rexc::StdlibSymbolPolicy::All;
	auto result = rexc::analyze_module(parsed.module(), diagnostics, options);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}
```

- [x] **Step 4: Run sema tests**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
```

Expected: sema tests pass and clearly distinguish default prelude from all-stdlib compiler-internal policy.

- [x] **Step 5: Commit sema test cleanup**

```bash
git add tests/sema_tests.cpp
git commit -m "test: define default prelude sema behavior"
```

## Task 5: Update Docs And Roadmap

**Files:**
- Modify: `docs/ch03-semantics.md`
- Modify: `docs/roadmap.md`
- Modify: `README.md`

- [x] **Step 1: Document the default prelude in Chapter 3**

In `docs/ch03-semantics.md`, after the module visibility section and before local-name analysis, add:

```markdown
The default prelude is deliberately small. Rexy makes common string and console
helpers available as bare names so compact examples can stay readable:
`print`, `println`, `read_line`, string predicates such as `str_eq`, numeric
parsers such as `parse_i32`, primitive print/read helpers, and `panic`.

Bootstrap internals are not default bare names. Allocation helpers, raw memory
helpers, file/path/environment helpers, sentinel-result helpers, and `std_*`
bridge symbols must not be treated as language-global names. They remain part
of the hosted runtime, and the bridge functions that already have explicit
stdlib paths can be called through those paths, such as `std::io::println` and
`std::process::exit`.
```

- [x] **Step 2: Update README examples if needed**

Search for omitted bare helper names in README examples:

```bash
rg -n "alloc_|memset_u8|memcpy_u8|str_copy_to|file_|path_join|std_io_|std_process_|\\bexit\\(" README.md
```

If the command prints no lines, no README edit is needed. If it prints lines, rewrite those examples to use default-prelude names or explicit `std::...` bridge paths available today.

- [x] **Step 3: Mark the roadmap item done**

In `docs/roadmap.md`, under Phase 8 `Planned work`, change:

```markdown
- decide the default prelude policy and which names remain always available;
```

to:

```markdown
- decide the default prelude policy and which names remain always available; **done for the current module model**
```

- [x] **Step 4: Run lightweight docs checks**

Run:

```bash
rg -n "std_prelude|default prelude|DefaultPrelude|alloc_reset|memset_u8" README.md docs/ch03-semantics.md docs/roadmap.md docs/superpowers/plans/2026-04-26-default-prelude-policy.md
```

Expected: docs mention the intended policy without implying omitted helpers are
default bare user-facing names. Full build and test verification remains Task 6.

- [x] **Step 5: Commit docs**

```bash
git add README.md docs/ch03-semantics.md docs/roadmap.md docs/superpowers/plans/2026-04-26-default-prelude-policy.md
git commit -m "docs: document default prelude policy"
```

If `README.md` had no changes, run:

```bash
git add docs/ch03-semantics.md docs/roadmap.md docs/superpowers/plans/2026-04-26-default-prelude-policy.md
git commit -m "docs: document default prelude policy"
```

## Task 6: Final Verification

**Files:**
- No source edits expected.

- [x] **Step 1: Run the full build and unit executable**

```bash
cmake --build build
build/rexc_tests
```

Expected: build succeeds and every unit test prints `PASS`.

- [x] **Step 2: Run the full CTest suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected:

```text
100% tests passed, 0 tests failed out of 4
```

- [x] **Step 3: Check the working tree**

```bash
git status --short
```

Expected: clean working tree after commits.

- [x] **Step 4: Summarize behavior for the user**

Report:

```text
Default prelude now includes only user-facing console, string, parse/read, and panic helpers.
Bootstrap alloc, memory, file/path/env, sentinel-result, and bridge symbols are no longer bare names by default.
Explicit stdlib bridge paths such as std::io::println continue to work.

Verification:
- build/rexc_tests
- ctest --test-dir build --output-on-failure
```
