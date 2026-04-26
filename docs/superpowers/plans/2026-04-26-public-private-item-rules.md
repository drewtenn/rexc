# Public/Private Item Rules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finalize Rexc's current public/private module and item rules so the behavior is explicit, covered by semantic tests, and documented as the Phase 8 baseline.

**Architecture:** Keep visibility enforcement in semantic analysis, where the compiler already has a complete module/function/static table. Do not add new syntax or package-level export machinery in this slice. The implementation should make the existing policy obvious through named helper functions, add missing edge-case tests, and update user-facing docs to describe the rule precisely.

**Tech Stack:** C++17, Catch2-style tests in `rexc_tests`, ANTLR grammar already supports `pub`, Markdown docs.

---

## Final Policy For This Slice

Use this policy consistently in code, tests, and docs:

- A module segment declared without `pub` is visible to its parent module and to descendants of that private module.
- A private module segment is not visible to siblings, sibling descendants, or unrelated ancestors.
- A private function or static is visible only in the declaring module and that module's descendants.
- A parent cannot access a private item declared in a child module unless the item is `pub` and the module path to it is accessible.
- A `pub` item inside a private module is still hidden from requesters that cannot access every module segment in the path.
- `use` imports enforce the same visibility rules at the import declaration's module.
- Root-module private items remain package-internal for now: descendant modules can use them, but they are not a public API contract for future package imports.

## Files

- Modify: `src/sema.cpp`
  - Clarify visibility helper names and make module-vs-item policy explicit.
  - Keep table-building and name-resolution structure intact.
- Modify: `tests/sema_tests.cpp`
  - Add focused tests for private static access, parent/sibling/descendant boundaries, `pub` inside private modules, and module import aliases.
- Modify: `docs/ch03-semantics.md`
  - Replace the broad visibility paragraph with exact rules and examples.
- Modify: `docs/roadmap.md`
  - Mark the first Phase 8 planned item as the current completed slice once tests/docs pass.

## Task 1: Add Missing Visibility Tests

**Files:**
- Modify: `tests/sema_tests.cpp`

- [ ] **Step 1: Add tests for private statics and same-module access**

Insert these tests after `sema_accepts_private_call_within_same_module`:

```cpp
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
	    "  mod inner { fn read() -> i32 { return VALUE; } }\n"
	    "}\n",
	    diagnostics);

	REQUIRE(result.ok());
	REQUIRE(!diagnostics.has_errors());
}
```

- [ ] **Step 2: Add tests for parent/sibling rejection**

Insert these tests after `sema_accepts_descendant_call_to_ancestor_private_function`:

```cpp
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
```

- [ ] **Step 3: Add tests for `pub` items hidden by private modules**

Insert these tests after `sema_rejects_private_module_segment_from_sibling`:

```cpp
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
```

- [ ] **Step 4: Add tests for public path success**

Insert this test after `sema_accepts_pub_module_with_pub_function_from_sibling`:

```cpp
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
```

- [ ] **Step 5: Run focused tests and confirm the baseline**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
```

Expected: some new tests may fail if they expose an implementation gap. Existing tests should still compile. Record the exact failing test names before editing `src/sema.cpp`.

- [ ] **Step 6: Commit the tests**

```bash
git add tests/sema_tests.cpp
git commit -m "test: cover module visibility policy"
```

## Task 2: Make Visibility Policy Explicit In Semantic Analysis

**Files:**
- Modify: `src/sema.cpp`

- [ ] **Step 1: Rename and split helper intent**

In `src/sema.cpp`, replace the current helper block:

```cpp
bool is_descendant_or_same(const std::vector<std::string> &candidate,
                           const std::vector<std::string> &ancestor) const
```

with this clearer group:

```cpp
bool is_same_module(const std::vector<std::string> &lhs,
                    const std::vector<std::string> &rhs) const
{
	return lhs == rhs;
}

bool is_descendant_or_same_module(const std::vector<std::string> &candidate,
                                  const std::vector<std::string> &ancestor) const
{
	if (ancestor.size() > candidate.size())
		return false;
	for (std::size_t i = 0; i < ancestor.size(); ++i) {
		if (candidate[i] != ancestor[i])
			return false;
	}
	return true;
}

bool is_parent_module_of(const std::vector<std::string> &parent,
                         const std::vector<std::string> &child) const
{
	return parent == parent_module_path(child);
}
```

- [ ] **Step 2: Add policy-specific helpers**

Place these helpers immediately after `parent_module_path`:

```cpp
bool can_access_private_module_segment(
    const std::vector<std::string> &private_module,
    const std::vector<std::string> &requester) const
{
	return is_parent_module_of(requester, private_module) ||
	       is_descendant_or_same_module(requester, private_module);
}

bool can_access_private_item(const std::vector<std::string> &owner,
                             const std::vector<std::string> &requester) const
{
	return is_descendant_or_same_module(requester, owner);
}
```

- [ ] **Step 3: Update module visibility checks to use the policy helper**

In `is_module_accessible`, replace:

```cpp
auto parent = parent_module_path(prefix);
if (requester == parent || is_descendant_or_same(requester, prefix))
	continue;
```

with:

```cpp
if (can_access_private_module_segment(prefix, requester))
	continue;
```

- [ ] **Step 4: Update item visibility checks to use the policy helper**

Replace the body of `is_item_visible` with:

```cpp
return visibility == ast::Visibility::Public ||
       can_access_private_item(owner, requester);
```

Then replace any remaining calls to `is_descendant_or_same` with `is_descendant_or_same_module`.

- [ ] **Step 5: Run focused tests**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
```

Expected: all semantic tests pass.

- [ ] **Step 6: Commit the semantic cleanup**

```bash
git add src/sema.cpp tests/sema_tests.cpp
git commit -m "refactor: make visibility policy explicit"
```

## Task 3: Remove Cascading Diagnostics For Rejected Imports

**Files:**
- Modify: `src/sema.cpp`
- Modify: `tests/sema_tests.cpp`

- [ ] **Step 1: Add a regression test for private import cascades**

Add this test after `sema_rejects_use_of_private_function`:

```cpp
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
```

- [ ] **Step 2: Extend `ImportKind` with an invalid marker**

In `src/sema.cpp`, replace:

```cpp
enum class ImportKind { Function, Global, Module };
```

with:

```cpp
enum class ImportKind { Function, Global, Module, Invalid };
```

- [ ] **Step 3: Record rejected imports as invalid aliases**

In `build_import_table`, introduce an alias before access checks:

```cpp
std::string alias = use.import_path.back();
auto &scope = imports_[canonical_path(use.module_path)];
```

When a function/global/module exists but fails its access check, record the alias and continue:

```cpp
scope[alias] = ImportInfo{ImportKind::Invalid, target_key, use.import_path};
continue;
```

Do this in each failing branch:

```cpp
if (!is_function_accessible(functions_.at(target_key), use.module_path,
                            use.location, target_key)) {
	scope[alias] = ImportInfo{ImportKind::Invalid, target_key, use.import_path};
	continue;
}
```

Use the same pattern for globals and modules.

- [ ] **Step 4: Preserve duplicate import diagnostics**

After computing `alias` and `scope`, keep duplicate detection before inserting a successful alias:

```cpp
if (scope.find(alias) != scope.end()) {
	diagnostics_.error(use.location, "duplicate import '" + alias + "'");
	continue;
}
```

For invalid aliases, run this duplicate check before assigning `ImportKind::Invalid` so repeated denied imports still report `duplicate import`.

- [ ] **Step 5: Suppress unknown-call cascades for invalid function aliases**

Add this helper near `find_import`:

```cpp
bool is_invalid_import_alias(const std::string &alias) const
{
	auto import = find_import(alias);
	return import != nullptr && import->kind == ImportKind::Invalid;
}
```

In the call-expression branch of `check_expr`, replace:

```cpp
if (function == nullptr) {
	diagnostics_.error(call.location, "unknown function '" + call.callee + "'");
} else {
```

with:

```cpp
if (function == nullptr) {
	if (call.callee_path.size() != 1 || !is_invalid_import_alias(call.callee_path[0])) {
		diagnostics_.error(call.location, "unknown function '" + call.callee + "'");
	}
} else {
```

- [ ] **Step 6: Run focused tests**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
```

Expected: semantic tests pass, including the no-cascade private import test.

- [ ] **Step 7: Commit diagnostic cleanup**

```bash
git add src/sema.cpp tests/sema_tests.cpp
git commit -m "fix: avoid import visibility diagnostic cascades"
```

## Task 4: Document The Finalized Rule

**Files:**
- Modify: `docs/ch03-semantics.md`
- Modify: `docs/roadmap.md`

- [ ] **Step 1: Update Chapter 3 visibility wording**

In `docs/ch03-semantics.md`, replace the paragraph beginning with `Visibility is enforced at the module boundary.` with:

```markdown
Visibility is enforced at every module segment and at the final item. A module
declared without `pub` is visible to its parent and to code inside that module's
subtree, but not to siblings or unrelated modules. A private function or static
is visible in the declaring module and in descendant modules. Parent modules
cannot reach into a child module's private items; the child must expose a `pub`
item, and every module segment in the path must also be accessible.

That means `pub fn value` inside a private `mod inner` is still hidden from a
sibling. The item is public only after the requester can name the module path
that contains it. `use` declarations follow the same rule as qualified calls:
an import is checked from the module where the `use` appears.
```

- [ ] **Step 2: Add a compact example**

Immediately after that paragraph, add:

````markdown
```rust
mod outer {
    fn shared() -> i32 { return 1; }

    pub mod api {
        pub fn value() -> i32 { return outer::shared(); }
    }

    mod internal {
        pub fn hidden() -> i32 { return 2; }
    }
}

fn main() -> i32 {
    return outer::api::value();
}
```

The root can call `outer::api::value` because `api` and `value` are public.
It cannot call `outer::internal::hidden`, even though `hidden` is marked `pub`,
because `internal` is private outside `outer`.
````

- [ ] **Step 3: Update roadmap Phase 8**

In `docs/roadmap.md`, under Phase 8 `Planned work`, change:

```markdown
- finalize public/private item rules;
```

to:

```markdown
- finalize public/private item rules; **done for the current module model**
```

Do not mark the whole phase complete.

- [ ] **Step 4: Run docs-adjacent verification**

Run:

```bash
cmake --build build --target rexc_tests
build/rexc_tests "sema*"
```

Expected: semantic tests still pass after docs-only changes.

- [ ] **Step 5: Commit docs**

```bash
git add docs/ch03-semantics.md docs/roadmap.md
git commit -m "docs: define module visibility policy"
```

## Task 5: Final Verification

**Files:**
- No source edits expected.

- [ ] **Step 1: Run the full unit test executable**

```bash
cmake --build build
build/rexc_tests
```

Expected: all unit tests pass.

- [ ] **Step 2: Run the full CTest suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all configured tests pass, including CLI and smoke tests.

- [ ] **Step 3: Check working tree**

```bash
git status --short
```

Expected: clean working tree after the task commits, or only intentional uncommitted changes if the user asked not to commit.

- [ ] **Step 4: Summarize behavior for the user**

Report:

```text
Finalized visibility policy:
- private modules are visible to parent and descendants only;
- private items are visible to declaring module and descendants only;
- public items remain hidden if any containing module segment is private;
- use imports enforce the same checks as qualified calls.

Verification:
- build/rexc_tests
- ctest --test-dir build --output-on-failure
```
