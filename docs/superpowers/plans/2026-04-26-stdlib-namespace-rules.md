# Stdlib Namespace Rules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Rexy's stdlib namespace rules explicit so `std::...` bridge paths, user modules, and lowered symbols cannot drift into ambiguous behavior.

**Architecture:** Keep this slice inside the existing name/sema/lower pipeline. Replace the implicit `std_` splitting rule with one explicit bridge-path helper, then make sema and lowering consume that helper consistently. This does not add ordinary stdlib module loading; it only clarifies the bridge namespace that already exists.

**Tech Stack:** C++17, existing `names.hpp` helpers, embedded Rexy stdlib source, Catch2-style `rexc_tests`, Markdown docs.

---

## Final Policy For This Slice

Use this policy consistently in code, tests, and docs:

- The reserved source namespace is the `std` module root and its bridge-backed child modules.
- Only bridge-backed `std_*` symbols map to `std::...` paths.
- Current bridge prefixes are:
  - `std_io_` -> `std::io::...`
  - `std_process_` -> `std::process::...`
  - `std_env_` -> `std::env::...`
  - `std_fs_` -> `std::fs::...`
  - `std_path_` -> `std::path::...`
- Public helpers without a bridge prefix, such as `alloc_bytes`, `memset_u8`, and `strlen`, do not gain `std::alloc::...`, `std::core::...`, or other synthetic paths in this slice.
- User modules named `std` or inside `std::...` are rejected while stdlib symbols are active.
- User module names that merely start with `std_` are allowed as source modules unless their lowered symbol collides with a reserved stdlib/runtime symbol.
- `StdlibSymbolPolicy::None` leaves stdlib namespace reservation disabled for tests and internal stdlib compilation.

## Files

- Modify: `src/names.hpp`
  - Rename or replace `stdlib_path_for_symbol()` with `stdlib_bridge_path_for_symbol()`.
  - Implement an explicit bridge-prefix table.
- Modify: `src/sema.cpp`
  - Use the bridge helper for stdlib module reservation and synthetic bridge functions.
  - Keep lowered-symbol collision checks from the default-prelude policy.
- Modify: `src/lower_ir.cpp`
  - Use the same bridge helper when registering lowered stdlib bridge calls.
- Modify: `src/stdlib/stdlib.cpp`
  - Use the same bridge helper for `find_stdlib_function("std::...")` lookups.
- Modify: `tests/stdlib_tests.cpp`
  - Add focused bridge-path mapping coverage.
- Modify: `tests/sema_tests.cpp`
  - Add namespace reservation and user-module boundary tests.
- Modify: `tests/ir_tests.cpp`
  - Add a lowering test proving an explicit bridge path lowers to the intended `std_*` symbol.
- Modify: `README.md`, `docs/ch03-semantics.md`, `docs/roadmap.md`
  - Document the explicit namespace rule and mark the Phase 8 item done after verification.

## Task 1: Define Bridge Path Mapping Tests

**Files:**
- Modify: `tests/stdlib_tests.cpp`
- Modify: `src/names.hpp`
- Modify: `src/stdlib/stdlib.cpp`

- [ ] **Step 1: Add failing stdlib bridge mapping tests**

In `tests/stdlib_tests.cpp`, add this include near the other includes:

```cpp
#include "../src/names.hpp"
```

Then add this test after `stdlib_declares_all_public_functions`:

```cpp
TEST_CASE(stdlib_exposes_only_bridge_backed_std_paths)
{
	auto io_println = rexc::stdlib_bridge_path_for_symbol("std_io_println");
	REQUIRE(io_println.has_value());
	REQUIRE_EQ(rexc::canonical_path(*io_println), std::string("std::io::println"));

	auto process_exit = rexc::stdlib_bridge_path_for_symbol("std_process_exit");
	REQUIRE(process_exit.has_value());
	REQUIRE_EQ(rexc::canonical_path(*process_exit), std::string("std::process::exit"));

	auto env_get = rexc::stdlib_bridge_path_for_symbol("std_env_get");
	REQUIRE(env_get.has_value());
	REQUIRE_EQ(rexc::canonical_path(*env_get), std::string("std::env::get"));

	auto fs_open_read = rexc::stdlib_bridge_path_for_symbol("std_fs_open_read");
	REQUIRE(fs_open_read.has_value());
	REQUIRE_EQ(rexc::canonical_path(*fs_open_read), std::string("std::fs::open_read"));

	auto path_join = rexc::stdlib_bridge_path_for_symbol("std_path_join");
	REQUIRE(path_join.has_value());
	REQUIRE_EQ(rexc::canonical_path(*path_join), std::string("std::path::join"));

	REQUIRE(!rexc::stdlib_bridge_path_for_symbol("std_core_strlen").has_value());
	REQUIRE(!rexc::stdlib_bridge_path_for_symbol("std_alloc_bytes").has_value());
	REQUIRE(!rexc::stdlib_bridge_path_for_symbol("std_io_").has_value());

	REQUIRE(rexc::stdlib::find_stdlib_function("std::io::print") != nullptr);
	REQUIRE(rexc::stdlib::find_stdlib_function("std::io::println") != nullptr);
	REQUIRE(rexc::stdlib::find_stdlib_function("std::io::read_line") != nullptr);
	REQUIRE(rexc::stdlib::find_stdlib_function("std::process::exit") != nullptr);
	REQUIRE(rexc::stdlib::find_stdlib_function("std::env::get") != nullptr);
	REQUIRE(rexc::stdlib::find_stdlib_function("std::fs::open_read") != nullptr);
	REQUIRE(rexc::stdlib::find_stdlib_function("std::fs::create_write") != nullptr);
	REQUIRE(rexc::stdlib::find_stdlib_function("std::path::join") != nullptr);

	REQUIRE(rexc::stdlib::find_stdlib_function("std::alloc::bytes") == nullptr);
	REQUIRE(rexc::stdlib::find_stdlib_function("std::core::strlen") == nullptr);
	REQUIRE(rexc::stdlib::find_stdlib_function("std::io_println") == nullptr);
}
```

- [ ] **Step 2: Run stdlib tests and verify the missing helper fails or the new expectations fail**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "stdlib*"
```

Expected before implementation: compile failure because `stdlib_bridge_path_for_symbol` is not declared.

- [ ] **Step 3: Replace the implicit path splitter with an explicit bridge helper**

In `src/names.hpp`, replace `stdlib_path_for_symbol` with this helper:

```cpp
inline std::optional<std::vector<std::string>> stdlib_bridge_path_for_symbol(
    const std::string &symbol)
{
	struct BridgePrefix {
		const char *prefix;
		const char *module;
	};
	static constexpr BridgePrefix prefixes[] = {
	    {"std_io_", "io"},
	    {"std_process_", "process"},
	    {"std_env_", "env"},
	    {"std_fs_", "fs"},
	    {"std_path_", "path"},
	};

	for (const auto &entry : prefixes) {
		std::string prefix(entry.prefix);
		if (symbol.rfind(prefix, 0) != 0 || symbol.size() == prefix.size())
			continue;
		return std::vector<std::string>{
		    "std", entry.module, symbol.substr(prefix.size())};
	}
	return std::nullopt;
}
```

- [ ] **Step 4: Update stdlib lookup to use the explicit helper**

In `src/stdlib/stdlib.cpp`, update `find_function_in`:

```cpp
const FunctionDecl *find_function_in(const std::vector<FunctionDecl> &functions,
                                     const std::string &name)
{
	for (const auto &function : functions) {
		if (function.name == name)
			return &function;
		if (auto path = stdlib_bridge_path_for_symbol(function.name);
		    path && canonical_path(*path) == name)
			return &function;
	}
	return nullptr;
}
```

- [ ] **Step 5: Run stdlib tests and verify they pass**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "stdlib*"
```

Expected: all stdlib tests pass, including `stdlib_exposes_only_bridge_backed_std_paths`.

- [ ] **Step 6: Commit the bridge mapping helper**

```bash
git add src/names.hpp src/stdlib/stdlib.cpp tests/stdlib_tests.cpp
git commit -m "feat: define explicit stdlib bridge paths"
```

## Task 2: Apply Bridge Mapping In Sema And Lowering

**Files:**
- Modify: `src/sema.cpp`
- Modify: `src/lower_ir.cpp`
- Test: `tests/sema_tests.cpp`
- Test: `tests/ir_tests.cpp`

- [ ] **Step 1: Add sema tests for allowed bridge paths and rejected synthetic paths**

In `tests/sema_tests.cpp`, add these tests near `sema_accepts_explicit_std_bridge_path_without_bare_bridge_name`:

```cpp
TEST_CASE(sema_accepts_current_std_bridge_modules)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 {\n"
	    "  std::io::print(\"hi\");\n"
	    "  std::io::println(\"hi\");\n"
	    "  std::process::exit(0);\n"
	    "  return 0;\n"
	    "}\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_synthetic_non_bridge_std_paths)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "fn main() -> i32 { return std::core::strlen(\"hi\"); }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("unknown function 'std::core::strlen'") !=
	        std::string::npos);
}
```

- [ ] **Step 2: Add an IR lowering test for bridge paths**

In `tests/ir_tests.cpp`, add this test after `lowering_mangles_user_module_function_symbols`:

```cpp
TEST_CASE(lowering_lowers_std_bridge_path_to_runtime_symbol)
{
	rexc::SourceFile source(
	    "test.rx",
	    "fn main() -> i32 { std::io::println(\"hi\"); return 0; }\n");
	rexc::Diagnostics diagnostics;
	auto parsed = rexc::parse_source(source, diagnostics);

	REQUIRE(parsed.ok());
	REQUIRE(rexc::analyze_module(parsed.module(), diagnostics).ok());

	auto module = rexc::lower_to_ir(parsed.module());
	const auto &statement =
	    static_cast<const rexc::ir::ExpressionStatement &>(*module.functions[0].body[0]);
	const auto &call = static_cast<const rexc::ir::CallValue &>(*statement.value);
	REQUIRE_EQ(call.callee, std::string("std_io_println"));
}
```

- [ ] **Step 3: Update sema to use `stdlib_bridge_path_for_symbol`**

In `src/sema.cpp`, replace every call to `stdlib_path_for_symbol(function.name)` with:

```cpp
stdlib_bridge_path_for_symbol(function.name)
```

Expected locations are stdlib module reservation in `build_module_table()` and bridge-function registration in `build_function_table()`.

- [ ] **Step 4: Update lowering to use `stdlib_bridge_path_for_symbol`**

In `src/lower_ir.cpp`, replace every call to `stdlib_path_for_symbol(function.name)` with:

```cpp
stdlib_bridge_path_for_symbol(function.name)
```

Expected locations are stdlib module registration and bridge-function registration in `build_function_table()`.

- [ ] **Step 5: Run sema and lowering tests**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
build/rexc_tests "lowering*"
```

Expected: the test runner may execute the full suite, but every printed test should pass.

- [ ] **Step 6: Commit sema/lowering bridge usage**

```bash
git add src/sema.cpp src/lower_ir.cpp tests/sema_tests.cpp tests/ir_tests.cpp
git commit -m "feat: apply explicit stdlib bridge namespaces"
```

## Task 3: Lock User Module Boundary Rules

**Files:**
- Modify: `tests/sema_tests.cpp`
- Modify: `src/sema.cpp` only if a new test exposes a mismatch.

- [ ] **Step 1: Add namespace boundary tests**

In `tests/sema_tests.cpp`, add these tests after the existing stdlib collision tests:

```cpp
TEST_CASE(sema_accepts_user_module_name_that_only_starts_with_std)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "mod std_io { pub fn custom() -> i32 { return 7; } }\n"
	    "fn main() -> i32 { return std_io::custom(); }\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_allows_user_std_module_when_stdlib_symbols_disabled)
{
	rexc::Diagnostics diagnostics;
	rexc::SemanticOptions options;
	options.stdlib_symbols = rexc::StdlibSymbolPolicy::None;
	auto result = analyze(
	    "mod std { pub fn custom() -> i32 { return 7; } }\n"
	    "fn main() -> i32 { return std::custom(); }\n",
	    diagnostics,
	    options);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}

TEST_CASE(sema_rejects_std_root_child_module_when_stdlib_symbols_are_active)
{
	rexc::Diagnostics diagnostics;
	auto result = analyze(
	    "mod std { pub mod custom { pub fn value() -> i32 { return 7; } } }\n"
	    "fn main() -> i32 { return std::custom::value(); }\n",
	    diagnostics);

	REQUIRE(!result.ok());
	REQUIRE(diagnostics.format().find("duplicate module 'std'") !=
	        std::string::npos);
}
```

- [ ] **Step 2: Run sema tests and observe behavior**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
```

Expected: `sema_accepts_user_module_name_that_only_starts_with_std` and `sema_allows_user_std_module_when_stdlib_symbols_disabled` pass. If `sema_rejects_std_root_child_module_when_stdlib_symbols_are_active` fails, update `src/sema.cpp` as described next.

- [ ] **Step 3: Keep reservation scoped to active stdlib symbols**

If needed, adjust `build_module_table()` so `reserve_stdlib_module_path()` is called only under `include_stdlib_symbols()`. The final shape should remain:

```cpp
void build_module_table()
{
	if (include_stdlib_symbols()) {
		reserve_stdlib_symbols();
		for (const auto &function : stdlib::stdlib_functions()) {
			if (auto path = stdlib_bridge_path_for_symbol(function.name)) {
				reserve_stdlib_module_path(*path);
				note_item_path(*path, true);
			}
		}
	}

	for (const auto &module : module_.modules) {
		// existing duplicate-module handling remains here
	}
}
```

- [ ] **Step 4: Run sema tests again**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
```

Expected: all sema tests pass.

- [ ] **Step 5: Commit user namespace boundary tests**

```bash
git add tests/sema_tests.cpp src/sema.cpp
git commit -m "test: lock std namespace boundary rules"
```

If `src/sema.cpp` did not change, use:

```bash
git add tests/sema_tests.cpp
git commit -m "test: lock std namespace boundary rules"
```

## Task 4: Update Docs And Roadmap

**Files:**
- Modify: `README.md`
- Modify: `docs/ch03-semantics.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/superpowers/plans/2026-04-26-stdlib-namespace-rules.md`

- [ ] **Step 1: Update README namespace wording**

In `README.md`, replace the paragraph that starts with `Public stdlib helpers outside the default prelude still emit` with:

```markdown
Public stdlib helpers outside the default prelude still emit into hosted
runtime builds. Only bridge-backed `std_*` declarations receive explicit
`std::...` paths; today those paths live under `std::io`, `std::process`,
`std::env`, `std::fs`, and `std::path`. Bootstrap allocation, raw memory,
sentinel-result, and portable core helpers are not default bare names and do
not receive synthetic `std::alloc` or `std::core` paths in this slice.
Compiler internals and tests that need those helpers opt into the explicit
`All` policy.
```

- [ ] **Step 2: Update Chapter 3 namespace wording**

In `docs/ch03-semantics.md`, replace the paragraph that starts with `Public standard-library helpers outside that default prelude still emit` with:

```markdown
Public standard-library helpers outside that default prelude still emit into
hosted runtime builds. Only bridge-backed `std_*` declarations receive
explicit `std::...` paths; today those paths live under `std::io`,
`std::process`, `std::env`, `std::fs`, and `std::path`. Bootstrap helpers, raw
memory helpers, runtime bridge labels, and stdlib statics are not user-facing
bare names. Compiler internals and tests that intentionally exercise those
names opt into an explicit all-stdlib symbol policy, while normal user analysis
keeps the smaller default prelude.
```

- [ ] **Step 3: Mark the roadmap item done**

In `docs/roadmap.md`, under Phase 8, change:

```markdown
- add clearer namespace rules for stdlib paths and user modules;
```

to:

```markdown
- add clearer namespace rules for stdlib paths and user modules; **done for
  the current bridge model**: `std::...` paths are explicit bridge-backed
  names only, `std` is reserved while stdlib symbols are active, and user
  `std_`-prefixed modules remain ordinary user modules unless their lowered
  labels collide with reserved stdlib/runtime symbols;
```

- [ ] **Step 4: Mark this plan's completed tasks**

In `docs/superpowers/plans/2026-04-26-stdlib-namespace-rules.md`, mark Tasks 1-4 steps complete as they are finished. Leave Task 5 unchecked until final verification passes.

- [ ] **Step 5: Run docs checks**

Run:

```bash
rg -n "std::alloc|std::core|all stdlib declarations|bridge-backed|std_.*modules" README.md docs/ch03-semantics.md docs/roadmap.md docs/superpowers/plans/2026-04-26-stdlib-namespace-rules.md
git diff --check
```

Expected: `std::alloc` and `std::core` appear only in text saying those paths are not created in this slice; `git diff --check` prints no whitespace errors.

- [ ] **Step 6: Commit docs**

```bash
git add README.md docs/ch03-semantics.md docs/roadmap.md docs/superpowers/plans/2026-04-26-stdlib-namespace-rules.md
git commit -m "docs: document stdlib namespace rules"
```

## Task 5: Final Verification

**Files:**
- No source edits expected.

- [ ] **Step 1: Run the full test executable**

```bash
cmake --build build --target rexc_tests
build/rexc_tests
```

Expected: build succeeds and every printed test is `PASS`.

- [ ] **Step 2: Run CTest**

```bash
ctest --test-dir build --output-on-failure
```

Expected:

```text
100% tests passed, 0 tests failed out of 4
```

- [ ] **Step 3: Check the branch**

```bash
git status --short
git log --oneline --max-count=10
```

Expected: clean working tree and recent commits for bridge mapping, sema/lowering usage, namespace boundary tests, and docs.

- [ ] **Step 4: Report the behavior**

Report this summary:

```text
Stdlib namespace rules are now explicit:
- bridge-backed std_* symbols map to known std::... paths only
- std is reserved while stdlib symbols are active
- user std_-prefixed modules are ordinary modules unless their lowered labels collide
- StdlibSymbolPolicy::None disables stdlib namespace reservation for internal/test use
```

## Self-Review

- Spec coverage: Tasks 1-3 cover explicit bridge mapping, sema/lowering usage, and user-module boundaries. Task 4 covers README, Chapter 3, roadmap, and plan status. Task 5 covers final verification.
- Placeholder scan: no placeholder markers remain; every code-changing step has concrete snippets and commands.
- Type consistency: the plan uses existing `StdlibSymbolPolicy`, `LowerStdlibSymbolPolicy`, `FunctionDecl`, `analyze`, and `lower_to_ir` names from the current codebase.
