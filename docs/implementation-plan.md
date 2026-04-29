# Rexy Implementation Plan

**Status:** Draft v1
**Created:** 2026-04-28
**Anchor PRD:** [`prd.md`](./prd.md)
**Companion to:** [`roadmap.md`](./roadmap.md) (milestone-level, established 2026-04-26)

## What This Document Is

This plan decomposes the [PRD's 8 phases](./prd.md#7-implementation-phases) into **63 numbered features** (FE-NNN / ST-N) with stable IDs, dependencies, effort signals, exit tests, and the compiler files each will touch. It is the bridge between the PRD (*what + why*) and execution (*work units a solo developer can pick up in order*).

The companion [`roadmap.md`](./roadmap.md) remains authoritative for high-level milestone phrasing and current-state narrative. Where the two overlap, the table at the bottom of this document maps PRD phases ↔ existing roadmap phases. **This plan extends, not replaces, that roadmap.**

## How To Read This

Each row is one work unit:

| Column | Meaning |
|---|---|
| **Done** | Progress marker. Use `[ ]` for open work and `[X]` only after the row's exit test passes |
| **ID** | Stable: `FE-001`–`FE-014` Phase 1, `FE-101`+ Phase 2, `FE-201`+ Phase 3, `FE-301`+ Phase 4, `FE-401`+ Phase 5, `FE-501`+ Phase 6, `FE-601`+ Phase 7, `ST-N` stretch |
| **Description** | One line, action-oriented |
| **Depends-on** | Other FE/ST IDs that must complete first (acyclic) |
| **Effort** | S = ≤1 day · M = 2–5 days · L = 1–2 weeks · XL = >2 weeks (solo developer) |
| **Exit test** | One concrete observable that proves the feature works |
| **Files** | Source files most likely to change; `*.rx` paths are under `src/stdlib/` |

## Where We Are Today (Phase 0 — already shipped)

From [`roadmap.md`](./roadmap.md) and source inspection:

- **Compiler:** ~7,500 LOC C++ across `src/{parse,sema,lower_ir,codegen_x86,codegen_arm64,types,ir,ast,target,diagnostics,main,source}.cpp` and `include/rexc/*.hpp`
- **Targets:** `i386-linux` / `i386-elf`, `i386-drunix`, `x86_64-linux` / `x86_64-elf`, `arm64-macos`
- **Language:** primitives, raw pointers, statics (scalar + buffer), modules (file + inline), `use` imports, qualified calls, `fn` / `extern fn`, control flow (`if`/`while`/`for`/`break`/`continue`), integer/bool/char `match`, arithmetic + boolean ops, casts (`as`), pre/postfix `++`/`--`, single-param handle types
- **Stdlib (already real):** `src/stdlib/core/{mem,num,str}.rx`, `src/stdlib/alloc/alloc.rx` (bump arena + `Result<i32>`-shaped helpers), `src/stdlib/std/{io,fs,path,process,time}.rx`, `src/stdlib/sys/runtime_*.cpp`
- **Tests:** 14 test files (frontend, sema, ir, codegen, codegen-arm64, stdlib, types, target, diagnostics, smoke, cli, parser-architecture, assemble, support)

## Adversarial Review Findings (2026-04-28)

This plan was challenged by two independent adversaries (codex/technical sequencing + gemini/solo-dev scope realism). Their convergent verdicts are load-bearing and must shape how this plan is read.

### Convergent finding #1 — Phase 4 (async) is the schedule killer

Both reviewers independently called out Phase 4 as the breaking point:

> **Codex:** "Phase 4 collectively requires a coroutine ABI, lifetime/escape model, executor abstraction, two backends, hosted runtime, Drunix runtime, and async I/O. That is not '6–9 months after generics'; it is a second compiler project."

> **Gemini:** "Phase 4 is where this project dies. Async logic requires deep integration with runtime, memory model, and backend. It's the least intellectually fun because it's 90% edge-case handling. **Delete Phase 4.** Async is a luxury. C, Zig (pre-0.10), and many others survived without a native async/await keyword."

**Decision posture (until further notice):** Phase 4 is **demoted to a P1 conditional** — implement *only after* Phases 1–3 are stable for ≥6 months, and *only if* a real Drunix workload is blocked on async (not on aspiration). The PRD's "first-class async" pillar (P0-4) remains the design intent, but the *implementation* of FE-301..FE-311 is gated on Phase 1–3 maturity. The plan's effort/calendar estimates below are *kept honest* by treating Phase 4 as optional rather than scheduled.

### Convergent finding #2 — The 2.5–3.5 year timeline is a fantasy

Gemini's precedents:
- **Zig (Andrew Kelley):** started 2015, full-time + foundation funding, still 0.13.0 in 2026 — 9 years.
- **Jai (Jonathan Blow):** started ~2014, still no general release after 10 years.

Solo-dev reality: "At year 3, you will spend 60% of your time fixing regressions in Phase 1 features while trying to implement Phase 4." Realistic ceiling for a *stable, usable* toolchain solo: **7–10 years**.

The plan's per-phase calendar feels are *engineering-time* estimates assuming no maintenance load. Treat them as floors, not ceilings.

### Convergent finding #3 — Phase 7 (self-hosting) at 12–18 months is not credible

Zig's Stage 2 (self-hosted) took roughly **3 years of focused effort *after* the language was mostly settled.** Self-hosting isn't a phase; it's a complete rewrite in a moving-target language. **Gate Phase 7 on Phases 1–3 having been stable for ≥2 years** before starting. The plan's 12–18 month estimate is kept here for accounting purposes but understood to be aspirational, not committed.

### Missing-from-plan item: maintenance debt

Neither the original nor the codex review accounted for:

- macOS / toolchain breakage (every Xcode update breaks something)
- AST/IR refactors as the language grows (gemini: "you'll refactor the AST for the fourth time because you realized raw pointers were a mistake")
- Test corpus regressions when features interact
- ANTLR runtime, lld, libc, sysroot churn

**Heuristic baked in below:** add a flat **30% maintenance overhead** on top of every phase's calendar feel. This is not in the per-feature effort tags (which are net engineering time); it lives at the calendar-feel level.

### Specific scope-creep traps to watch (gemini)

- **Phase 2 (generics):** "Start with simple templates and end up writing a Turing-complete trait-solver." → Mitigation: Open Question OQ-1 (PRD §9) commits to *no traits in v1*. Hold the line.
- **Phase 3 (comptime):** "Custom bytecode VM tangent for 'speed,' 4 months wasted." → Mitigation: FE-203 starts as a tree-walking interpreter. No VM until the AST interpreter is shown to be the bottleneck on a *real* derive macro.
- **Phase 6 (LSP):** "Polish a VS Code extension instead of fixing compiler bugs." → Mitigation: Gate FE-506/FE-507 on the *underlying* compiler diagnostics being good (FE-014). Fix the source before polishing the surface.

### Specific sequencing bugs found by codex (fixed below in the tables)

The following dependency / sequencing bugs were caught and corrected inline in the phase tables:

- **FE-011 / FE-012** (Option/Result + `?`) require real generics → moved their depends-on to include FE-103 (and accepted that "Phase 1" is in practice gated on Phase 2 to *complete* these two rows; the parser/sema work for the *types* can land in Phase 1, but the generic instantiation depends on Phase 2)
- **FE-013** (`unsafe` blocks) is independent of `?` → depends-on changed from FE-012 → FE-014
- **FE-101** (generic parsing) needs structs → depends-on adds FE-001
- **FE-303 / FE-304** swapped: await-validation precedes coroutine lowering → FE-303 now depends on FE-304
- **FE-303** also needs the executor contract before state-shape freezes → depends-on adds FE-307
- **FE-310** (`task_scope`) needs escape analysis → flagged as XL with explicit dep on ST-1 partial

### Specific effort re-tags (codex)

- **FE-014** L → **XL**: spans + JSON + 8-line cap + fix-it across parser/sema/main/diagnostics is its own subproject
- **FE-105** L → **XL** (or narrow scope): allocator-as-value without traits is harder than it looks
- **FE-109** XL → **split into 4 sub-features** (Vec, HashMap, allocator wiring, hash/eq contracts) — see table below
- **FE-203** XL kept but flagged as "research-project risk" — comptime IR interpreter has memory semantics, host/target split, termination, diagnostics, caching, purity policy
- **FE-310** L → **XL** (escape analysis is borrow-shaped)
- **FE-504** XL kept but acknowledged as "this is its own product, not a feature"

### Weak exit tests strengthened (codex)

- **FE-001:** "parses struct" → AST round-trip fidelity + span correctness
- **FE-007:** "compile-only" → tuple field access + ABI on both targets
- **FE-008:** "sema accepts" → bounds + len() runtime correctness
- **FE-103:** "two symbols link" → substitution + recursive generics + diagnostic spans
- **FE-203:** strengthened to "interpreter passes a test corpus of pure functions"
- **FE-307:** "executor symbol links" → ABI round-trip with a real (toy) executor
- **FE-610:** bit-identical output requires explicit determinism engineering — added as a precondition note

---

## Next 5 Features (Start Here)

1. [X] **FE-014** — Standardize diagnostics (spans, JSON, 8-line cap). Independent of types work; can land first to set the floor for everything that follows.
2. [X] **FE-001** — Parse struct declarations and field types.
3. [X] **FE-002** — Add struct type symbols and field lookup.
4. [X] **FE-003** — Lower and emit struct literals/field access.
5. [X] **FE-004** — Parse enum declarations with payload variants.

Rationale: FE-014 is the diagnostics floor required by every future error message in the PRD. FE-001..003 unblock structs across all three pipeline tiers (parse/sema/lowering+codegen). FE-004 starts the enum track in parallel since it shares only the parser layer.

---

## Phase 1 — Type System Foundations

Maps to: PRD Phase 1, [`roadmap.md`](./roadmap.md) Phase 9 + Phase 13's diagnostics polish.
PRD exit gate: at least one Drunix `user/apps/*.rx` rewrites to use a struct + enum + Option/Result with no raw pointers in the safe path.

| Done | ID | Description | Depends-on | Effort | Exit test | Files |
|:---:|---|---|---|:---:|---|---|
| [X] | FE-001 | Parse struct declarations and field types | — | M | parses `struct Point { x: i32, y: i32 }`; AST round-trips with correct spans | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp, include/rexc/ast.hpp |
| [X] | FE-002 | Add struct type symbols and field lookup | FE-001 | M | sema accepts `p.x` on `Point`; rejects `p.z` with span pointing at offending name | src/sema.cpp, src/types.cpp, include/rexc/types.hpp |
| [X] | FE-003 | Lower and emit struct literals/field access (both targets) | FE-002 | L | compile-run returns field value from struct on x86_64 + arm64; struct-by-value param ABI matches sysv-amd64 + AAPCS | src/lower_ir.cpp, src/ir.cpp, src/codegen_x86.cpp, src/codegen_arm64.cpp |
| [X] | FE-004 | Parse enum declarations with payload variants | — | M | parses `enum E { A, B(i32) }`; AST round-trips | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp |
| [X] | FE-005 | Type-check enum construction and tags | FE-004 | L | sema rejects unknown variant; tagged-union layout decided + documented | src/sema.cpp, src/types.cpp |
| [X] | FE-006 | Lower and emit tagged enum values (both targets) | FE-005 | L | local macOS compile-run verifies enum tag + payload extraction on x86_64/arm64 paths; Drunix x86 QEMU smoke runs an `i386-drunix` enum binary and prints `FE006 PASS` | src/lower_ir.cpp, src/ir.cpp, src/codegen_x86.cpp, src/codegen_arm64.cpp |
| [X] | FE-007 | Add tuple types and tuple expressions | — | M | compile-run accesses `pair.0`/`pair.1`; tuple-by-value ABI proven on both targets | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp, src/sema.cpp, src/types.cpp |
| [X] | FE-008 | Add first-class slice type `&[T]` (replaces ad-hoc `*T + len` pattern) | FE-002 | M | runtime `len()` correct; OOB index test catches the overflow with span | src/types.cpp, src/sema.cpp, src/stdlib/core/mem.rx |
| [X] | FE-009 | Implement struct/enum destructuring in `match` patterns | FE-003, FE-006 | L | match destructures `Some(x)` and binds | grammar/Rexy.g4, src/parse.cpp, src/sema.cpp |
| [X] | FE-010 | Add exhaustiveness checks for enum match | FE-006, FE-009 | M | missing enum arm emits error pointing to uncovered variant | src/sema.cpp |
| [X] | FE-011 | Generalize stdlib `Option<T>` and `Result<T,E>` as compiler-recognized prelude types; source-level generic enum declarations still wait for FE-103 | FE-006, **FE-103** | M | compile-only test uses `Option<i32>` and `Result<i32, MyErr>` from prelude | src/stdlib/alloc/alloc.rx, src/stdlib/core/option.rx + result.rx, src/types.cpp, src/sema.cpp |
| [X] | FE-012 | Implement `?` result propagation operator | FE-011 | M | compile-run propagates `Err` through `?` | grammar/Rexy.g4, src/parse.cpp, src/sema.cpp, src/lower_ir.cpp |
| [X] | FE-013 | Enforce `unsafe` blocks for raw deref / unsafe extern call | **FE-014** (independent of `?`) | M | raw deref outside `unsafe` is a compile error | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp, src/sema.cpp |
| [X] | FE-014 | Standardize diagnostics: spans everywhere, `--diag=json`, 8-line cap, fix-it suggestion field | — | **XL** *(was L; codex review caught this — span plumbing + JSON schema + UX fix-its is 3 sub-features rolled into one)* | `--diag=json` snapshot test passes; every existing parser/sema error carries a span; one fix-it suggestion shipped end-to-end | src/diagnostics.cpp, include/rexc/diagnostics.hpp, src/main.cpp, src/sema.cpp, src/parse.cpp |
| [X] | FE-015 | i386 aggregate-by-value ABI (≤ 8 bytes via EDX:EAX) — required to land Phase 1's exit gate on Drunix's i386 userland | FE-003, FE-006, FE-007 | M | `user/apps/phase1test.rx` compiles for `--target=i386-drunix`; codegen test asserts `movl %edx, %eax` round-trip on a struct return | src/types.cpp, src/codegen_x86.cpp, tests/codegen_tests.cpp |

> **Phase 1 exit-gate status:** met. [`user/apps/phase1test.rx`](../../../user/apps/phase1test.rx) exercises struct + payload-bearing enum + `match` destructuring + `Result<i32>` + `?` in safe code (no raw pointers, no `unsafe`); compiles for `i386-drunix`, `x86_64-linux`, and `arm64-macos`; arm64-macos host run returns exit 0. The PRD criterion ("Drunix can rewrite at least one C user app … using structs + enums + Option/Result, no raw pointers in the safe path") is satisfied via the FE-015 i386 ABI work that landed alongside this gate.

> **Phase 1 scoping note (post-review):** FE-011/FE-012 nominally live in Phase 1 (they shape Phase 1's exit gate — "Drunix app uses Option/Result without raw pointers") but cannot *complete* until Phase 2's FE-103 monomorphization is done. Practically: write the parser/sema work for Option/Result during Phase 1; finish them at the start of Phase 2. The Phase 1 exit gate is then a milestone, not a hard barrier.

> **Backend verification rule:** backend features are not complete on assembly inspection alone. They must pass the local macOS host test path for supported host targets, and any feature that affects Drunix userland must also run under the relevant Drunix QEMU target. For current x86 userland this means an `i386-drunix` binary booted and executed on Drunix x86.

## Phase 2 — Generics + Memory Model

Maps to: PRD Phase 2, [`roadmap.md`](./roadmap.md) Phases 10 + 11.
PRD exit gate: `Vec<T>` and `HashMap<K, V>` exist in stdlib, parameterized by allocator.

| Done | ID | Description | Depends-on | Effort | Exit test | Files |
|:---:|---|---|---|:---:|---|---|
| [X] | FE-101 | Parse generic `fn` and `struct` parameters | FE-014, **FE-001** *(struct examples need structs)* | M | parses `fn id<T>(x: T) -> T` and `struct Vec<T>` | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp |
| [X] | FE-102 | Resolve generic type parameters in sema | FE-101 | L | sema accepts generic identity fn | src/sema.cpp, src/types.cpp |
| [X] | FE-103 | Monomorphize generic fns and structs | FE-102 | XL | `id<i32>` and `id<bool>` link as separate symbols; recursive `Tree<T>` works; instantiation diagnostic carries span at the call site (not template body) | src/lower_ir.cpp, src/ir.cpp, src/types.cpp |
| [X] | FE-104 | Emit readable generic instantiation diagnostics | FE-103 | M | bad generic call points at the *call site*, not the template (FR-025) | src/sema.cpp, src/diagnostics.cpp |

> **FE-103 status:** done. Generic *function* monomorphization (per-instantiation mangled symbols, type inference from arg types, span-correct diagnostics), non-recursive generic *struct* monomorphization (`Box<i32>`/`Pair<i32, bool>`-shape templates instantiate at every type-position use, struct literals adopt the monomorph from the expected type, per-instantiation layouts computed independently in sema and lower_ir), and **recursive `Tree<T>` self-references** all land. The recursive case works by pre-registering the mangled name *before* substituting fields so `*Tree<T>` resolves to itself, unlocking the path to `Vec<T>`/`HashMap<K,V>` (FE-109).
| [X] | FE-105 | Model allocator as explicit value/parameter (interface contract) | FE-103 | **XL** *(was L; codex flagged: allocator-as-interface without traits is hard to scope)* | low-level `Arena` / `arena_*` round-trips, and the `Vec::with_alloc(arena)` pattern is demonstrated end-to-end (constructor takes `*Arena`, allocates from it, push/get round-trip exits 60) | src/stdlib/alloc/alloc.rx, src/stdlib/stdlib.cpp, src/sema.cpp, src/lower_ir.cpp |

> **FE-105 verification note (2026-04-30):** done. The `Vec::with_alloc(arena)` pattern wording from the exit test is closed by `examples/arena_vec_demo.rx` and `tests/smoke_tests.cpp::smoke_vec_with_alloc_round_trip` (commit `874add9`). The fixture is `ArenaVec_i32` (type-specific, not generic): `arena_vec_i32_with_alloc(arena: *Arena, capacity: i32) -> *ArenaVec_i32` allocates both header and backing storage from the supplied arena, and `push` / `get` round-trip values; the binary smoke on Darwin arm64 (`tests/assemble_smoke.sh`) asserts `exit==60`. Two pre-existing language gaps were surfaced and routed to FE-109a / a future codegen ticket: (1) `*Vec<T>` lookups inside a generic body are not resolved by sema today (blocks the generic version), and (2) `>=`-3-field struct-by-value return on arm64 only round-trips the first field (forced the `*ArenaVec_i32` return shape). Real generic `Vec<T>` lands in FE-109a.
| [X] | FE-106 | Refactor stdlib `alloc.rx` to expose arena allocator as a stdlib type (today: implicit static) | FE-105 | M | arena allocator round-trip smoke test passes | src/stdlib/alloc/alloc.rx |
| [X] | FE-107 | Add `defer` statement and scope-cleanup lowering | FE-106 | M | defer runs on early return and on normal exit | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp, src/sema.cpp, src/lower_ir.cpp |
| [X] | FE-108 | Implement definite-assignment analysis | FE-107 | L | use-before-init emits error with span | src/sema.cpp |
| [ ] | FE-109a | Generic `Vec<T>` (parameterized by allocator) | FE-103, FE-105, FE-108 | L | `Vec<i32>` and `Vec<MyStruct>` push/pop/iter pass tests | src/stdlib/std/vec.rx (new), src/sema.cpp |
| [ ] | FE-109b | Hash + equality contract (concrete-typed, no trait system) | FE-109a | M | hash and `==` work for primitives + struct of primitives | src/stdlib/core/hash.rx (new), src/sema.cpp |
| [ ] | FE-109c | Generic `HashMap<K,V>` (parameterized by allocator) | FE-109a, FE-109b | L | `HashMap<i32, str>` insert/lookup/iter passes tests | src/stdlib/std/hashmap.rx (new) |
| [ ] | FE-109d | Stdlib collections test suite + benchmarks | FE-109a, FE-109c | M | snapshot tests pass; basic perf numbers recorded | tests/stdlib_tests.cpp |

## Phase 3 — Comptime + Reflection

Maps to: PRD Phase 3 (no equivalent in current roadmap.md — this is new design territory).
PRD exit gate: a `Debug`-print derive works via comptime reflection, no compiler magic.

> **Open Question OQ-3 (PRD §9):** Is `comptime` a separate construct (Zig) or are types first-class values everywhere (Jai)? PRD recommends Zig-style. Resolve before FE-201.

| Done | ID | Description | Depends-on | Effort | Exit test | Files |
|:---:|---|---|---|:---:|---|---|
| [ ] | FE-201 | Parse `comptime` blocks and expressions | FE-103 | M | parses `comptime { ... }` and `comptime expr` | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp |
| [ ] | FE-202 | Represent types as compile-time values | FE-201 | L | comptime receives `type(Point)` as a value | src/types.cpp, src/sema.cpp, src/ir.cpp |
| [ ] | FE-203 | Execute restricted Rexy IR at compile time (tree-walking interpreter — **NO bytecode VM in v1**) | FE-202 | XL ⚠️ research-risk | interpreter passes a corpus of pure functions: arithmetic, struct field access, enum match, recursion bounded by depth limit | src/lower_ir.cpp, src/ir.cpp, src/sema.cpp |
| [ ] | FE-204 | Expose struct field iteration to comptime | FE-203, FE-003 | M | comptime loop sees field names and types | src/types.cpp, src/sema.cpp, src/stdlib/core/reflect.rx (new) |
| [ ] | FE-205 | Expose enum variant iteration to comptime | FE-203, FE-006 | M | comptime loop sees variants and payload types | src/types.cpp, src/sema.cpp, src/stdlib/core/reflect.rx |
| [ ] | FE-206 | Parse and attach user attributes (`#[name]`) | FE-201 | M | parses `#[derive(Debug)] struct S` | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp, src/sema.cpp |
| [ ] | FE-207 | Implement `derive(Debug)` via comptime reflection (proof point — **gated on FE-203 being a real interpreter, not stubs**) | FE-204, FE-205, FE-206 | L | derived `Debug` prints struct + enum without compiler magic | src/stdlib/core/reflect.rx, src/stdlib/std/fmt.rx (new), src/sema.cpp, src/lower_ir.cpp |

> **Phase 3 scope-creep trap (gemini):** "You'll decide you need a custom bytecode VM for 'speed,' wasting 4 months on an internal interpreter that has nothing to do with the language's core mission." → Mitigation locked: FE-203 is a tree-walking AST interpreter. No VM, ever, until a concrete derive macro is provably the bottleneck on a real workload.

## Phase 4 — Async — ⚠️ DEFERRED / GATED

> **Adversarial-review verdict:** Both reviewers independently called this phase the project killer. Codex: "a second compiler project, not 6–9 months after generics." Gemini: "this is where the project dies; **delete Phase 4**."
>
> **Decision posture:** Phase 4 is **demoted to a P1 conditional**. Implement *only after* Phases 1–3 are stable for ≥6 months *and* a real Drunix workload is concretely blocked on async (not on aspiration). The PRD's "first-class async" pillar (P0-4) remains the design intent — the *implementation* of FE-301..FE-311 is held until that gate is met. Use simple threads / callbacks until then.
>
> The decomposition below is preserved as a *reference design*, not a committed schedule.

Maps to: PRD Phase 4 (no equivalent in current roadmap.md — entirely new).
PRD exit gate: 50-LOC async TCP echo compiles, runs under stdlib executor on Linux + a Drunix-specific executor in the kernel proving ground.

> **Open Question OQ-2 (PRD §9):** Colored vs colorless `async`. PRD recommends colored. Lock before FE-301; see PRD Risk R-2.
> **Open Question OQ-5 (PRD §9):** Panic / cancellation model interacts with executor design. Decide before FE-307.

| Done | ID | Description | Depends-on | Effort | Exit test | Files |
|:---:|---|---|---|:---:|---|---|
| [ ] | FE-301 | Parse `async fn` and async call types | FE-103 | M | parses `async fn read()` and `await call()` shapes | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp |
| [ ] | FE-302 | Add `Future<T>` contract to stdlib | FE-301 | M | sema accepts `Future<i32>` as return type | src/stdlib/core/future.rx (new), src/types.cpp, src/sema.cpp |
| [ ] | FE-304 | Implement `await` expression validation *(was FE-303 — order swapped per codex review: validate before lower)* | FE-301, FE-302 | L | `await` outside `async` emits error | grammar/Rexy.g4, src/parse.cpp, src/sema.cpp |
| [ ] | FE-307 | Define pluggable executor interface (link-time, per PRD R-5) | FE-302 | L | executor ABI round-trip with a toy executor; signatures stable across hosted vs freestanding | src/stdlib/core/executor.rx (new), src/sema.cpp |
| [ ] | FE-303 | Lower async fn into coroutine state struct (state-machine transform) | FE-304, FE-307, FE-003 *(also implicit: capture/liveness analysis, escape analysis)* | XL ⚠️ "second compiler project" risk | async fn lowers to IR state machine with stable resume points; capture analysis correct for nested awaits | src/lower_ir.cpp, src/ir.cpp |
| [ ] | FE-305 | Emit coroutine resume code on x86_64 | FE-303 | XL | x86_64 async timer smoke test runs to completion | src/codegen_x86.cpp |
| [ ] | FE-306 | Emit coroutine resume code on arm64 | FE-303 | XL | arm64 async timer smoke test runs | src/codegen_arm64.cpp |
| [ ] | FE-308 | Implement default hosted executor (Linux/macOS) | FE-307, FE-305 | L | Linux async timer demo runs with stdlib executor | src/stdlib/std/executor.rx (new) |
| [ ] | FE-309 | Implement freestanding Drunix executor hook | FE-307 | L | Drunix executor link smoke passes (separate module from FE-308) | src/stdlib/sys/executor_drunix.rx (new) |
| [ ] | FE-310 | Add `task_scope` structured concurrency primitive | FE-308, **partial escape analysis (overlap with ST-1)** | **XL** *(was L; codex flagged: "cannot escape scope" is a borrow/lifetime problem)* | spawned task cannot escape its scope (compile error or runtime panic) | src/stdlib/std/scope.rx (new), src/sema.cpp |
| [ ] | FE-311 | Add async I/O — **split per codex review**: (a) timers, (b) sockets, (c) poll/reactor shim, (d) TCP abstraction, (e) Drunix-link proof | FE-308, FE-309 | XL × 5 sub-features | 50-LOC async TCP echo compiles + runs on Linux and links on Drunix | src/stdlib/std/net.rx (new), src/lower_ir.cpp |

## Phase 5 — Toolchain + Cross-Compile

Maps to: PRD Phase 5, [`roadmap.md`](./roadmap.md) Phase 12.
PRD exit gate: macOS host produces working ELFs for Linux/x86_64, Linux/arm64, and Drunix without third-party install.

| Done | ID | Description | Depends-on | Effort | Exit test | Files |
|:---:|---|---|---|:---:|---|---|
| [ ] | FE-401 | Define toolchain manifest format (target triple → linker + sysroot mapping) | — | M | manifest loads target triples; round-trip serialization test | src/main.cpp, src/target.cpp, include/rexc/target.hpp, new docs/toolchain-manifest.md |
| [ ] | FE-402 | Vendor and invoke `lld` (or mold) per target | FE-401 | L | `rexc` links without a system-installed linker | src/main.cpp, build/CMake glue |
| [ ] | FE-403 | Bundle Linux x86_64 sysroot | FE-402 | L | macOS host builds Linux/x86_64 hello-world end-to-end | src/target.cpp, src/main.cpp |
| [ ] | FE-404 | Bundle Linux arm64 sysroot | FE-402 | L | macOS host builds Linux/arm64 hello-world | src/target.cpp, src/main.cpp |
| [ ] | FE-405 | Bundle Drunix freestanding sysroot | FE-402 | L | `--profile=freestanding` links a Drunix-shaped binary | src/target.cpp, src/main.cpp, src/stdlib/sys/runtime_drunix.cpp |
| [ ] | FE-406 | Plumb canonical `--target=<triple>` triples; replace ad-hoc `--drunix-root` | FE-401 | M | unsupported triple errors clearly with suggestions | src/target.cpp, src/main.cpp |
| [ ] | FE-407 | Add cross-target smoke matrix (CI gate) | FE-403, FE-404, FE-405 | M | CI smoke matrix green: macOS host × {linux-x86_64, linux-arm64, drunix} | tests/cli_smoke.sh, CI config |

## Phase 6 — C Interop + LSP + Book

Maps to: PRD Phase 6, [`roadmap.md`](./roadmap.md) Phase 13.
PRD exit gate: real Drunix C library callable from rexy via `extern "C"`; LSP shows goto-def + diagnostics.

| Done | ID | Description | Depends-on | Effort | Exit test | Files |
|:---:|---|---|---|:---:|---|---|
| [ ] | FE-501 | Parse `extern "C"` ABI annotations | FE-014 | S | parses `extern "C" fn puts(s: *i8) -> i32;` | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp |
| [ ] | FE-502 | Type-check C-ABI-safe signatures + flag mismatches | FE-501, FE-013 | M | unsafe-C-call rule enforced; pointer type compatibility checked | src/sema.cpp, src/types.cpp |
| [ ] | FE-503 | Emit C ABI calls per target (sysv-amd64 + AAPCS) | FE-502 | L | calls libc `puts` on x86_64 and arm64 with correct register/stack discipline | src/codegen_x86.cpp, src/codegen_arm64.cpp |
| [ ] | FE-504 | Build header-to-rexy binding tool | FE-503 | XL | emits bindings for a small `.h` (e.g., `<unistd.h>` subset) | new tools/rexc-bindgen/, src/parse.cpp, src/ast.cpp |
| [ ] | FE-505 | Extract parser/sema facts for LSP (JSON dump) | FE-014 | L | JSON symbols include spans, types, doc-comments | src/parse.cpp, src/sema.cpp, src/types.cpp |
| [ ] | FE-506 | Implement LSP server: diagnostics streaming + hover types | FE-505 | L | VS Code shows squiggle + type hover on a `.rx` file | new tools/rexc-analyzer/, src/diagnostics.cpp, src/sema.cpp |
| [ ] | FE-507 | Implement LSP goto-def + completion (basic) | FE-505 | L | LSP goto-def test passes; completion suggests in-scope symbols | new tools/rexc-analyzer/, src/sema.cpp, src/parse.cpp |
| [ ] | FE-508 | Write reference-book chapters for new language surface | FE-109, FE-207, FE-311 | M | `make docs` builds chapters covering types, generics, comptime, async | docs/ch07*.md (new), docs/build.mk |
| [ ] | FE-509 | Package compiler, stdlib, LSP, sysroots into single release archive | FE-407, FE-506 | L | release archive installs cleanly on macOS / Linux; smoke tests pass | src/main.cpp, packaging scripts (new) |

## Phase 7 — Self-Hosting

Maps to: PRD Phase 7, [`roadmap.md`](./roadmap.md) Phase 15.
PRD exit gate: `rexc` source is rexy.

> **Decision OQ before FE-602:** full rewrite vs gradual port. Recommendation: stage0 (C++ rexc) compiles stage1 (rexy rexc) until parity, then bootstrap.

| Done | ID | Description | Depends-on | Effort | Exit test | Files |
|:---:|---|---|---|:---:|---|---|
| [ ] | FE-601 | Decide bootstrap strategy and stage layout (stage0/stage1/stage2) | FE-509 | M | documented stage plan committed | new docs/bootstrap.md |
| [ ] | FE-602 | Port lexer/token model to rexy | FE-601 | L | rexy lexer snapshots match C++ on the test corpus | new src/rexy/lexer.rx |
| [ ] | FE-603 | Port parser to rexy AST | FE-602 | XL | rexy parser parses the existing compiler test inputs | new src/rexy/parse.rx, src/rexy/ast.rx |
| [ ] | FE-604 | Port semantic analyzer to rexy | FE-603 | XL | sema snapshots match C++ across the test corpus | new src/rexy/sema.rx, src/rexy/types.rx |
| [ ] | FE-605 | Port IR + lowering to rexy | FE-604 | XL | IR snapshots match C++ | new src/rexy/lower_ir.rx, src/rexy/ir.rx |
| [ ] | FE-606 | Port x86_64 codegen to rexy | FE-605 | XL | x86_64 assembly snapshots match | new src/rexy/codegen_x86.rx |
| [ ] | FE-607 | Port arm64 codegen to rexy | FE-605 | XL | arm64 assembly snapshots match | new src/rexy/codegen_arm64.rx |
| [ ] | FE-608 | Port diagnostics engine to rexy | FE-604 | L | diagnostic snapshots match | new src/rexy/diagnostics.rx |
| [ ] | FE-609 | Build stage1 compiler with stage0 (rexy rexc compiled by C++ rexc) | FE-606, FE-607, FE-608 | XL | stage1 compiles all `examples/*.rx` | new src/rexy/main.rx |
| [ ] | FE-610 | Prove self-host bit-identical output (stage1 compiles itself; stage2 == stage1) | FE-609, **explicit determinism engineering** *(deterministic codegen ordering, stable symbol mangling, reproducible linker invocation — codex flagged this as a precondition)* | XL | stage1 and stage2 binaries / assembly are byte-identical on macOS host targeting Linux/x86_64 | tests/self_host_bit_identical.sh (new) |

## Phase 8+ — Stretch

Lower-fidelity decomposition. These sit on top of Phase 7's complete language.

| Done | ID | Description | Depends-on | Effort | Exit test | Files |
|:---:|---|---|---|:---:|---|---|
| [ ] | ST-1 | Prototype region/borrow checker | FE-108, FE-610 | XL | rejects an escaping borrowed local | src/sema.cpp, src/types.cpp |
| [ ] | ST-2 | Add macro system only where comptime falls short | FE-207 | XL | macro expands before sema; round-trip test | grammar/Rexy.g4, src/parse.cpp, src/ast.cpp |
| [ ] | ST-3 | Implement package manager manifest | FE-509 | XL | `rexc build` resolves a local dep | src/main.cpp, src/parse.cpp |
| [ ] | ST-4 | Build `rexfmt` from parser AST | FE-603 | L | formatter snapshot suite green | new tools/rexfmt/, src/parse.cpp, src/ast.cpp |
| [ ] | ST-5 | Build linter on sema facts | FE-507 | L | unused-binding lint appears in LSP and CLI | src/sema.cpp, src/diagnostics.cpp |
| [ ] | ST-6 | Add Windows target backend + link path | FE-407 | XL | Windows hello-world executable links | src/target.cpp, src/main.cpp, src/codegen_x86.cpp |

---

## Cross-Phase Dependency Notes

- **FE-303 coroutine lowering requires FE-103 monomorphization and FE-003 aggregate layout.** Async cannot precede generics or structs — coroutine state machines are generated structs parameterized by their captured locals.
- **FE-207 `derive(Debug)` is a proof of comptime reflection, not a compiler intrinsic.** If FE-207 needs compiler magic, FE-203/FE-204/FE-205 are incomplete.
- **FE-311 async I/O depends on FE-307 executor separation.** Building async I/O against the default executor before the freestanding executor exists creates an abstraction that won't survive Drunix.
- **FE-504 binding generation depends on FE-503 real ABI emission.** Parsing C headers without an actual ABI to bind to produces dead code.
- **FE-610 self-host bit-identical depends on FE-014 stable diagnostics and FE-509 deterministic packaging.** Non-deterministic compiler output kills self-host parity tests.
- **FE-014 (diagnostics floor) is a prerequisite for FE-101 (generics) and FE-501 (extern C).** Both need the structured-error format to make their diagnostics legible.
- **FE-501 (extern "C") becomes meaningful only after FE-013 (`unsafe`).** Calling out across an ABI boundary is inherently `unsafe`.

## Mapping: PRD Phases ↔ Existing roadmap.md Phases

| PRD Phase | This plan's FE range | Existing `roadmap.md` phase |
|---|---|---|
| Phase 0 (current) | n/a | Phases 0–8 (already shipped) |
| Phase 1 — types | FE-001…FE-014 | Phase 9 (Rich Data Representation) + Phase 13 (Diagnostics polish) |
| Phase 2 — generics + memory | FE-101…FE-109 | Phase 10 (Allocation, Ownership, Collections) + Phase 11 (Generics, Traits, Formatting) |
| Phase 3 — comptime / reflection | FE-201…FE-207 | *(no equivalent — new ground)* |
| Phase 4 — async | FE-301…FE-311 | *(no equivalent — new ground)* |
| Phase 5 — toolchain + cross-compile | FE-401…FE-407 | Phase 12 (Host Portability, Runtime, Target Maturity) |
| Phase 6 — C interop + LSP + book | FE-501…FE-509 | Phase 13 (Diagnostics, Tooling, DX) |
| Phase 7 — self-hosting | FE-601…FE-610 | Phase 15 (Self-Hosting Horizon) |
| Phase 8+ stretch | ST-1…ST-6 | Phase 14 (Optimization) and various |

[`roadmap.md`](./roadmap.md) Phase 14 (Optimization) is intentionally not represented in this plan — the PRD treats optimization as "later, after semantics are stronger" and this implementation plan inherits that posture.

## Effort Summary (rough, solo developer — net engineering time, NOT including 30% maintenance overhead)

> **Important:** Calendar feels below are **net engineering time** at sustained focus. The "Realistic" column adds the 30% maintenance overhead the adversarial review flagged as missing. The "If async cut" column reflects the gemini-recommended posture of demoting Phase 4.

| Phase | Features | Effort breakdown | Engineering time | Realistic (incl. maintenance) |
|---|---:|---|---|---|
| Phase 1 — types | 14 | 1×S, 9×M, 3×L, **1×XL** *(FE-014 reclassified)* | ~4–6 mo | ~5–8 mo |
| Phase 2 — generics + memory | 12 *(FE-109 split into 4)* | 0×S, 4×M, 5×L, **3×XL** *(FE-105 reclassified)* | ~5–8 mo | ~7–10 mo |
| Phase 3 — comptime / reflection | 7 | 0×S, 4×M, 2×L, 1×XL ⚠️ research-risk | ~3–5 mo | ~4–7 mo |
| Phase 4 — async ⚠️ DEFERRED | 11 | 0×S, 2×M, 4×L, **5×XL** | ~9–12 mo *(if attempted)* | ~12–16 mo *(if attempted)* |
| Phase 5 — toolchain | 7 | 0×S, 3×M, 4×L, 0×XL | ~2–4 mo (parallelizable) | ~3–5 mo |
| Phase 6 — interop + LSP + book | 9 | 1×S, 2×M, 5×L, 1×XL | ~4–6 mo | ~5–8 mo |
| Phase 7 — self-hosting | 10 | 0×S, 1×M, 2×L, 7×XL | ~12–18 mo *(aspirational; see warning)* | ~16–24 mo |
| Phase 8+ stretch | 6 | 0×S, 0×M, 2×L, 4×XL | open-ended | open-ended |

**Total core (Phase 1 → 7), revised:**

| Scenario | Calendar feel |
|---|---|
| Original plan (engineering time only) | 2.5–3.5 years |
| Realistic (with maintenance overhead, async included) | **5–8 years** *(consistent with Zig precedent)* |
| Realistic (with maintenance, **async deferred per gemini's cut**) | **3.5–5 years** |
| Realistic (Phases 1–3 + 5 + 6 only — defer self-host AND async) | **2–3 years** ← *recommended posture for committed scope* |

**The PRD's R-11 honesty:** success ceiling is "respected alternative + Drunix runs on it," not mass adoption. The realistic posture above reinforces that — and explicitly invites the user to consider committing only to Phases 1–3 + 5 + 6, with Phase 4 (async) and Phase 7 (self-host) as long-horizon aspirations rather than scheduled work.

### Maintenance overhead detail (the line item the original plan missed)

The 30% overhead absorbs:
- macOS / Xcode / Apple Silicon toolchain breakage (every major macOS release)
- ANTLR runtime updates, lld version churn, libc/sysroot updates
- AST/IR refactors as the language grows ("you'll refactor the AST a 4th time when you realize raw pointers were a mistake" — gemini)
- Test corpus regressions when features interact (especially Phase 1 ↔ Phase 2, Phase 2 ↔ Phase 3)
- Drunix-side breakage when its kernel ABI shifts

This is non-negotiable in a solo project. Treat the 30% as a budget; if it's not used, that's slack for the next phase, not a gift.

## How To Use This Plan

1. **Pick a feature from "Next 5 Features"** above — it respects the dependency graph.
2. **Open the row** to confirm files-touched and exit test.
3. **Spawn `/octo:embrace "implement FE-NNN: <description>"`** for multi-AI execution, or just open the file and start.
4. **Run the exit test, then mark the row `[X]`** before moving to the next feature.
5. **Update `roadmap.md`** when a *phase* completes (not per feature) — that doc tracks milestones.

For phase-level execution, use:
```
/octo:embrace "implement Phase 1 of the Rexy implementation plan (FE-001 through FE-014)"
```
