# Rexy Language PRD

**Status:** Draft v1
**Owner:** Drew Tennenbaum
**Created:** 2026-04-28
**Scope:** The Rexy language and `rexc` compiler — vision, audience, design pillars, scope, and phased delivery.
**Out of scope (this doc):** Detailed feature specifications, syntax design RFCs, the per-feature roadmap (P0/P1/P2 — covered by a separate roadmap doc that uses this PRD as input).

---

## 1. Executive Summary

**Rexy is a systems programming language built to fix the things C++ has refused to fix in 30 years** — undefined behavior as the default mode, the preprocessor as the build model, diagnostics that no junior can read, async and reflection that arrived bolted-on a decade late, and a cross-compile story that requires a research project. Rexy treats those as language-level failures, not "quality of implementation" issues, and designs them out from day one.

It is a no-GC, no-runtime, no-libc-required language with the same hardware proximity and predictable cost C++ developers depend on — minus the 40-year backward-compatibility tax. Where Rust earned its discipline (readable diagnostics, the `unsafe` boundary, modules instead of headers), rexy adopts the discipline. Where Rust paid too high a cognitive tax (mandatory borrow checker on every function, viral `async`, type-system gymnastics for routine code), rexy doesn't follow.

`rexc` is the first compiler. It is host-portable (macOS / Linux / Windows for the developer experience) and targets x86_64 and arm64. **Drunix** — a hobby/educational kernel in this repo — is rexy's primary proving ground: a real OS that boots, runs userland, and exercises the language end-to-end without standing on the shoulders of a libc that already exists.

**Positioning (external pitch):** *Frictionless Concurrency.* The only systems language where async, compile-time reflection, and modules are co-designed from day one — where the compiler tells you what's wrong in plain English, where there is exactly one way to encapsulate code, and where cross-compiling from a Mac to a Linux server or a bare-metal kernel is a single command, not a weekend.

**Key value (one line):** A 2026-native systems language — engineered against C++'s failure modes, not against C++ itself.

---

## 2. Problem Statement

### 2.1 What rexy adopts from Rust (and what it doesn't)

Rust is not the structural enemy. Rust earned its discipline, and rexy inherits the parts that earned it. But Rust also overshot in places where the cost-benefit no longer makes sense for systems code that the author already understands. Rexy's relationship to Rust is *philosophical mentor, not symmetric peer*.

| Rust property | Rexy's stance |
|---|---|
| Readable diagnostics | **Adopt.** Mandate it, harder. (P0-1) |
| `unsafe` boundary | **Adopt.** Default code is safe; UB requires `unsafe`. (P0-3) |
| Modules instead of headers | **Adopt.** Rexy goes further: no preprocessor at all. (P0-2) |
| `Option`/`Result`-style error model | **Adopt.** Plus the `?` operator. (FR-008) |
| Cargo ecosystem | **Defer.** Excellent tool; not in v1 scope. (NG-4) |
| Mandatory borrow checker on every function | **Reject for v1.** Borrow-checking is the *whole product* of Rust; lifting one knob and bolting it on doesn't work, and the cognitive tax doesn't pay back on kernel-internal code where the author already knows the lifetimes. (NG-2) |
| Viral `async` coloring with `Send`/`Sync` constraints | **Reject.** Rexy is colored (FR-009) but does not propagate trait constraints virally. The async ABI is a calling-convention concern, not a type-system tax. |
| Tokio/async-std/smol ecosystem split | **Reject.** Pluggable executor at link time, with a single canonical stdlib executor. (FR-010) |
| Compile times | **Improve, don't match.** Modules + comptime should beat both Rust's macro-heavy compilation and C++'s template-heavy one. |
| `Pin`, `Arc<Mutex<T>>`, lifetime gymnastics on routine code | **Reject.** Rexy's type system is intentionally less expressive on this axis. Memory model details in §3 / NG-2 / FR-019. |

### 2.2 The C++ pain points (rexy is engineered against this list)

These are ranked by severity for systems work in 2026. Each row maps to a rexy design goal; that mapping is the structural argument for why this language needs to exist.

| # | C++ failure mode | Concrete impact | Rexy fix |
|---|---|---|---|
| **C-1** | **UB is the default mode** | Use-after-free, OOB access, data races, strict aliasing, signed overflow, uninitialized reads — all silent. No syntactic boundary says "this scope is dangerous." Audits cannot be scoped. CISA and NSA both publicly recommend leaving C++ on memory-safety grounds. | Opt-in `unsafe` blocks. Safe subset is the default. (P0-3 / FR-002 / FR-019) |
| **C-2** | **Preprocessor + `#include` are still the build model** | Headers, ODR violations, recompilation cascades, include-order bugs, macros leaking across translation units. C++20 modules exist on paper; tooling is still fractured in 2026. Every downstream C++ failure (build times, IDE responsiveness, link-time chaos) descends from this. | Modules as the *only* encapsulation unit. No headers, no `#include`, no preprocessor. (P0-2 / FR-001) |
| **C-3** | **Diagnostics are unreadable** | A single wrong concept or missing `constexpr` produces 200 lines of instantiation backtrace. The committee has treated diagnostics as "quality of implementation" for 30 years. Vendors haven't fixed it. | Diagnostics are a P0 design pillar with hard rules: 8-line cap, no instantiation noise, source span + suggestion mandatory. Diagnostics are a code-review gate. (P0-1 / FR-003 / FR-004 / FR-024 / FR-025) |
| **C-4** | **40-year backward-compatibility tax** | Trigraphs, `register`, `throw()`, narrowing conversions, integer promotion, ABI lock-in (`std::regex` permanently slow, `std::unordered_map` permanently wrong). The committee can add escape hatches but not remove mistakes. | Greenfield language. No inherited mistakes. Stability promise *starts* in v1; legacy is not part of v1. |
| **C-5** | **Async / coroutines / executors arrived bolted-on** | `co_await` shipped C++20; standard executors (P2300) only landed in C++26. Industry split into Boost.Asio, libuv, raw threads, fibers. There is still no idiomatic "spawn this future" answer in vanilla C++ in 2026. | Async designed in from day one. ABI and calling convention know about it. Pluggable executor at link time. Structured concurrency. Completion-based primitive. (P0-4 / FR-009–012) |
| **C-6** | **No package manager, no build standard** | CMake, Bazel, Meson, vcpkg, conan, system, git submodules — pick five, none compose. Every project re-invents its build. This is why language features (modules, coroutines) take a decade to actually adopt. | `rexc` builds end-to-end without delegating to a meta-build. Package manager deferred (NG-4) but the build is *one tool*, not a stack. |
| **C-7** | **Reflection only just arrived (C++26)** | P2996 ships with heavyweight compiler-internal lifetimes and a syntax that requires a modern toolchain. Rust spent the last decade with macros + procedural reflection while C++ waited. | Compile-time reflection is a P0 core feature, not a 2026 retrofit. Built on a Zig-style comptime base. (P0-5 / FR-013 / FR-014 / FR-015) |
| **C-8** | **Cross-compilation is a research project** | Need: target compiler, sysroot, libstdc++/libc++ matching, target headers, linker, sometimes a custom CMake toolchain file. Compare to `rustup target add … && cargo build --target=…`. C++ has no answer. | macOS host → Linux/x86_64, Linux/arm64, freestanding/Drunix as a single command. The `rexc` distribution ships the toolchain. (P0-7 / FR-020–023) |
| **C-9** | **Compile times scale poorly with templates** | Heavy template code makes incremental builds painful. Direct consequence of header-based compilation + lack of build artifact sharing. | Modules + comptime obviate header recompilation cascades. Compile speed is a non-functional requirement, tracked. |

### 2.3 The status quo for "switch to a new systems language"

| Language | What works | What doesn't |
|---|---|---|
| **Zig** | Simplicity, allocators, comptime, real cross-compile. The closest spiritual relative to rexy. | No safety story. Async was tried, removed, and is being redesigned. Type system feels under-specified for large programs. |
| **Carbon** | Explicit C++ interop story. | Still C++ semantics underneath. Migration tool, not a new paradigm. Vaporware risk. |
| **Odin** | `defer`, context allocator, dead-simple. Ships real software. | No safety story. Implicit context threading is a footgun at scale. |
| **Hare** | Radically simple, real cross-compile. | Too conservative — no generics, no async. |
| **Mojo** | Owns AI/ML systems space. | Python-superset framing rules it out for kernel work. |
| **Jai** | `#run` compile-time execution, fast compile times. | Closed development, no spec, single-vendor risk. |

**The gap rexy fills:** A language with Rust's *philosophy* (readable errors, `unsafe` boundary, hard module discipline) and Zig's *simplicity* (no heavyweight type tax, comptime as the metaprogramming model), with async + reflection designed in from day one rather than bolted on. None of the above ships that combination.

### 2.4 Drunix-specific motivation

Rexy is co-designed with **Drunix**, a hobby/educational OS in this repo. Today Drunix's userland is C. Porting Drunix's user apps and (eventually) kernel modules to rexy is rexy's first real-world workload — and Drunix is rexy's regression test for "does this language actually work on hardware without a libc safety net?"

---

## 3. Goals & Metrics

### 3.1 Design pillars (P0 — non-negotiable)

| # | Pillar | Why |
|---|---|---|
| **P0-1** | **Readable error messages, by design.** Diagnostics are a feature, not a polish item. Every error must include source span, suggestion, and the constraint that was violated, in human English. | C++ template-error walls are the #1 reason developers won't recommend C++ to juniors. Rust's diagnostics are the #1 reason developers tolerate the borrow checker. Pick the right side once. |
| **P0-2** | **Modules as the only encapsulation unit.** No headers. No `#include`. No preprocessor. No ODR. A module is a directory or a file; visibility is `pub` or default-private; cross-module use is `use foo::bar`. | Every C++ project's complexity tax begins with the preprocessor. Every Rust project's clarity begins with the module system. Adopt the second; refuse the first. |
| **P0-3** | **Opt-in `unsafe` blocks.** Default rexy code is safe-ish (definite assignment, no out-of-bounds where statically provable, no UB-by-default). `unsafe { ... }` is the only place you can deref raw pointers, call ABI-unsafe externs, or do undefined-behavior tricks. | C++ is "all unsafe all the time." Audits cannot be scoped. Rust's `unsafe` boundary makes safety auditable. Copy that boundary. |
| **P0-4** | **First-class async, designed in from day one.** The ABI, calling convention, and type system know about async. Not a library bolt-on. | Rust's async is a 5-year retrofit; C++ coroutines arrived 10 years before standard executors did. Both shipped accidents. Rexy designs async into the bones. |
| **P0-5** | **Compile-time reflection as a core feature.** Code can introspect types, fields, and attributes at compile time. No runtime reflection cost. | Zig's comptime is the proof that this works in a no-GC systems language. C++26 finally agrees. Make it native, not a feature flag. |
| **P0-6** | **No garbage collection, ever.** Allocation is explicit. Allocators are values. | Non-negotiable for the audience. Drunix kernel code cannot have a GC. |
| **P0-7** | **Cross-compilation from macOS is a first-class workflow, not a side effect.** The rexc distribution must ship the toolchain bits required to target Linux/x86_64 and Linux/arm64 from a macOS host without the user installing a third-party cross GCC. | Cross-compile is a distribution promise, not just a `--target` flag. Rust solved this with rustup targets + lld + musl. Rexy must own the same vertical. |

### 3.2 Strategic positioning (P1 — drives adoption)

| # | Goal | Success signal |
|---|---|---|
| **P1-1** | **Frictionless Concurrency** as the marketing pitch. Where Rust is "fearless concurrency," rexy is async-by-default ergonomics. | First-time users write a working async TCP echo in <50 LOC without reading a book. |
| **P1-2** | **Drunix as the killer artifact.** Rexy's success is anchored to running Drunix end-to-end, not to ecosystem size. | Drunix userland ports off C. Eventually a Drunix kernel module compiles in rexy. |
| **P1-3** | **Self-hosting compiler.** rexc eventually compiles itself. | `rexc` source is rexy. |
| **P1-4** | **C interop without a wrapper.** rexy can `extern "C"` declare and link against a C library directly. (C++ interop is **explicitly P2** — see non-goals.) | Drunix can call existing C utility code without FFI ceremony. |

### 3.3 Polish (P2 — quality bar)

| # | Goal |
|---|---|
| **P2-1** | LSP from day one of the language being usable for real work. (`rexc-analyzer` parity with `rust-analyzer` is a long-term aim.) |
| **P2-2** | A formal language reference document, kept in sync with the compiler. |
| **P2-3** | A formatter (`rexfmt`) and a linter — built-in, not vendored. |
| **P2-4** | Reproducible builds across host platforms. |

### 3.4 Success metrics

| Metric | P0 (must) | P1 (target) | P2 (stretch) |
|---|---|---|---|
| Drunix userland ported | 50% of `user/apps/*.rx` ports compile and run | All `user/apps/*.rx` compile and run | Drunix kernel module compiles in rexy |
| Compiler self-hosting | n/a | n/a | rexc self-hosts |
| First-time user productivity | Hello-world in 5 min on macOS | Async echo server in 30 min | TCP/HTTP demo in 1 hr |
| Diagnostics quality | Every parse + sema error has source span + message | Every error has a suggested fix | Diagnostics include "did you mean" suggestions for misspelled idents/types |
| Cross-compile | macOS → Linux/x86_64 hello world without third-party install | macOS → Linux/arm64 same | macOS → bare-metal Drunix without third-party install |
| LSP | n/a (not yet) | Goto-def, hover types, error squiggles | Refactors, code actions |

---

## 4. Non-Goals

These are explicit *no*s. The PRD treats this list as load-bearing — anything that drifts onto it requires re-opening the PRD.

| # | Non-goal | Reason |
|---|---|---|
| **NG-1** | **No garbage collection.** Not even opt-in. Not even debug-mode. | Audience cannot use a GC. Settles the design space. |
| **NG-2** | **No Rust-style borrow checker in v1.** Memory safety in the *safe* subset is provided by simpler means (no raw deref outside `unsafe`, definite assignment, bounds checks where provable). Lifetimes and a borrow checker may be added later, but are *not* part of v1. | Implementing a sound borrow checker is the cost of half a compiler. Vale-style linear types are similarly expensive. Defer. |
| **NG-3** | **No C++ interop in v1.** C interop yes; C++ no. | C++ ABI is a tar pit. Carbon is the cautionary tale. |
| **NG-4** | **No package manager in v1.** Modules are filesystem-relative; cross-project dependencies are out of scope until the language is stable. | Cargo is excellent and *also* drove Rust's ecosystem fragmentation around async. We are not racing to recreate that. |
| **NG-5** | **No macros in v1.** Compile-time reflection (`comptime`-like) covers the metaprogramming budget. | Procedural macros are a parallel feature surface. Reflection should obsolete most uses. |
| **NG-6** | **No GUI / web / mobile stdlib.** Rexy's stdlib is a *systems* stdlib. | Scope discipline. |
| **NG-7** | **No competing with Mojo for AI/ML.** Rexy is not the AI language. | Different audience, different design center. |
| **NG-8** | **No Windows as a *target* in v1.** macOS and Linux as targets; Windows as a host (developer machine) only. | Triage. |
| **NG-9** | **No hidden control flow.** Following Zig's principle: no operator overloading that does I/O, no exceptions, no implicit allocation. | Predictability is a feature. |

---

## 5. User Personas

### Persona A — "Senior Rust engineer with type-system fatigue"

- **Profile:** 5+ years professional Rust. Ships infra at a company that adopted Rust during the 2021–2024 wave.
- **Pain:** Spending 30% of cognitive budget fighting the type system on internal code where the team already understands the lifetimes. `Pin` and async-trait gymnastics in particular.
- **What rexy must offer:** The same readable diagnostics culture, the same `unsafe` boundary, but without the obligation to encode every lifetime in the type system for code where it doesn't earn its keep. First-class async that isn't viral.
- **Why they switch:** They'd rather write rexy than another `Arc<Mutex<HashMap<K, V>>>`.
- **What blocks them:** Missing LSP, missing async stdlib, the suspicion that they'll lose Rust's correctness without getting the speed back.

### Persona B — "Senior C++ engineer who has seen one too many template-error walls"

- **Profile:** 10+ years professional C++. Writes performance-critical systems (game engines, embedded, financial infra).
- **Pain:** Cryptic template diagnostics, modules that don't quite work in their build system, ABI roulette, "always unsafe."
- **What rexy must offer:** A language as fast and as low-level as C++ but with diagnostics they'd actually show a junior, modules that work, and a syntactic boundary for the unsafe parts so audits can be scoped.
- **Why they switch:** They've watched Rust eat their adjacent niche for 5 years and were waiting for something less zealous.
- **What blocks them:** Lack of mature C interop (C++ interop is a non-goal, but C interop must be effortless), no clear stdlib for systems primitives (sockets, files, threads).

### Persona C — "OS hacker / Drunix contributor"

- **Profile:** Hobby/educational OS work. Reads kernel source for fun. Currently writes Drunix in C.
- **Pain:** C is fine but archaic. Wants generics for collections, modules without `#include`, and pattern matching on enums for IPC handling — without giving up freestanding/no-libc.
- **What rexy must offer:** No-runtime, no-libc-required, freestanding-friendly compilation. Clean inline assembly story. Predictable codegen. The ability to write a kernel module someday.
- **Why they switch:** Rexy is *for* Drunix. Drunix is *for* rexy.
- **What blocks them:** They are the most patient of the three personas, but if rexy can't generate clean position-independent code or has any runtime hidden in startup, they'll bail.

---

## 6. Functional Requirements

Numbered FRs. Each entry is one requirement, traceable from a P0/P1/P2 goal above.

### Language core

- **FR-001** (P0-2) Modules as files/dirs. Visibility via `pub`. Cross-module access via `use a::b::c`. (✅ Already implemented in current `rexc`.)
- **FR-002** (P0-3) `unsafe` blocks. Within the safe subset, raw-pointer deref and ABI-unsafe extern calls are compile errors *outside* an `unsafe` block. (Currently rexc has raw pointers but no safe/unsafe boundary.)
- **FR-003** (P0-1) Every diagnostic has: source span, message, optional fix-it suggestion. Diagnostics are uniformly formatted.
- **FR-004** (P0-1) `rexc` exits with non-zero on any diagnostic of severity ≥ error and prints diagnostics in a stable, machine-parseable format (JSON via `--diag=json`) for IDEs.
- **FR-005** Type system v1: `i8..i64`, `u8..u64`, `bool`, `char`, `str` literal, raw pointer `*T` (✅ done), **plus** `struct`, `enum` (sum types), tuple, slice (`&[T]`).
- **FR-006** Generics on functions and structs (single-param to start; bounded later). Single-param handle types (`IDENT<T>`) already exist and serve as the bridgehead.
- **FR-007** Pattern matching on structs and enums (currently only integer/bool/char). Includes destructuring `let` and exhaustiveness checking.
- **FR-008** `Option<T>` and `Result<T, E>` are stdlib types and the canonical error model. `?` operator for short-circuit propagation.

### Async (P0-4)

- **FR-009** Colored async — `async fn` is a separate type from `fn`. Calling an `async fn` in non-async context requires an explicit executor entry point. *Rationale:* Zig's colorless async was implementation-intractable; Rust's coloring is honest.
- **FR-010** Pluggable executor at link time. The default executor lives in stdlib; freestanding/Drunix can ship an alternate. *No* hardcoded runtime.
- **FR-011** Structured concurrency: `spawn` only inside a scope (`nursery` / `task_scope`); tasks cannot outlive their scope. Unstructured `spawn` is not a v1 feature.
- **FR-012** Async I/O primitives are completion-based (io_uring-shaped) at the lowest level, with a poll-based shim where necessary. *Rationale:* matches the direction Linux is going; fits Drunix's eventual driver model.

### Reflection (P0-5)

- **FR-013** Compile-time code execution: a `comptime` construct (or equivalent) lets ordinary rexy code run at compile time over types-as-values. *Rationale:* Zig's model is the most defensible base for a no-GC systems language.
- **FR-014** Compile-time type introspection: iterate fields of a struct, query enum variants, read attributes. No runtime cost.
- **FR-015** Reflection extends to user-defined attributes on items (`#[some_attr]`-style). Attributes are first-class data accessible to comptime code.

### Memory model (relates to NG-2)

- **FR-016** Allocators are values. Functions that allocate take an allocator parameter (or use a default). *Rationale:* Zig and Odin both validate this design.
- **FR-017** Arena/region allocators are stdlib types. *Rationale:* Drunix-friendly; covers the common case.
- **FR-018** `defer` for scope-exit cleanup. *Rationale:* Odin/Zig validated; covers RAII's most common use.
- **FR-019** Within safe code: definite assignment (no use of uninitialized locals), no double-`drop`-equivalent, no dereferencing a pointer obtained outside `unsafe`.

### Toolchain (P0-7)

- **FR-020** `rexc --target=<triple>` works without third-party cross GCC. The `rexc` distribution ships an explicit toolchain manifest: target triples, linker (mold/lld), libc/freestanding stub.
- **FR-021** macOS host → Linux/x86_64 target produces a working ELF that runs under standard Linux glibc *and* under Drunix.
- **FR-022** macOS host → Linux/arm64 target produces a working ELF.
- **FR-023** A freestanding profile (`--profile=freestanding`) emits no-libc-needed code suitable for Drunix kernel/userland.

### Diagnostics (P0-1)

- **FR-024** No diagnostic exceeds 8 lines without an explicit "more details with `-Werbose`" footer.
- **FR-025** The compiler MUST not emit instantiation noise. If a generic instantiation fails, the diagnostic points to the call site, not to N levels of internal expansion.

### C interop (P1-4)

- **FR-026** `extern "C" fn name(...)` compiles, links, and matches the platform C ABI.
- **FR-027** Header-to-rexy bindings tool (Phase 4+). v1 may require manual `extern` declarations.

---

## 7. Implementation Phases

The technical research surfaced a critical sequencing constraint: **types → generics → async → reflection**. Async coroutine lowering touches types deeply; reflection requires stable type representations. Building either before structs/generics is a redesign trap. The phases below honor that.

### Phase 0 — Where rexc is today (✅ shipped)

Primitives, raw pointers, statics, modules, `fn`, control flow, integer/bool/char `match`, basic stdlib for I/O and strings, single-param handle types, x86_64 + arm64 codegen.

### Phase 1 — Type system foundations (P0)

- **FR-005** structs, enums, tuples, slices
- **FR-007** pattern matching on structs/enums (exhaustiveness)
- **FR-008** `Option`/`Result` + `?` operator
- **FR-002** `unsafe` boundary lands (was meaningless before, since everything was `*T`)
- **FR-003 / FR-004 / FR-024 / FR-025** diagnostics polish to "Rust-quality" baseline

**Exit criterion:** Drunix can rewrite at least one C user app in idiomatic rexy using structs + enums + Option/Result, no raw pointers in the safe path.

### Phase 2 — Generics + memory model (P0)

- **FR-006** generics on fn + struct (single-param, unbounded; bounds later)
- **FR-016** allocators as values
- **FR-017** arena allocator stdlib type
- **FR-018** `defer`
- **FR-019** definite-assignment + safe-subset rules

**Exit criterion:** A `Vec<T>` and a `HashMap<K, V>` exist in stdlib, parameterized by an allocator.

### Phase 3 — Comptime + reflection (P0-5)

- **FR-013** `comptime` execution
- **FR-014** type introspection
- **FR-015** user attributes

**Rationale for this position in the order:** comptime requires structs/generics to exist (because the most common use case is iterating struct fields). Building comptime before generics yields a half-feature.

**Exit criterion:** A `Debug`-print derive works via comptime reflection, no compiler magic.

### Phase 4 — Async (P0-4)

- **FR-009** `async fn` as colored functions
- **FR-010** pluggable executor
- **FR-011** structured concurrency primitive (`task_scope`)
- **FR-012** completion-based async I/O primitive

**Rationale for this position:** coroutine state machines are generated structs. They depend on Phase 1's struct support and Phase 2's generics. Building async before generics produces an executor that can only handle a fixed-shape future.

**Exit criterion:** A 50-LOC async TCP echo server compiles, runs under stdlib's default executor on Linux, and runs under a Drunix-specific executor in the kernel proving ground.

### Phase 5 — Toolchain + cross-compile (P0-7)

- **FR-020 / FR-021 / FR-022 / FR-023** cross-compile from macOS to Linux/x86_64, Linux/arm64, and freestanding/Drunix without third-party install.

**Why not earlier:** target codegen already works (rexc has x86_64 + arm64). The blocker for a real cross-compile workflow is the *distribution* — shipping the linker, sysroot, and freestanding stubs as part of `rexc`. That's a packaging problem, solvable in parallel with Phase 1–4 but tracked separately.

### Phase 6 — C interop + LSP + book (P1)

- **FR-026 / FR-027** C interop
- LSP server (`rexc-analyzer`)
- Language reference book (extends the existing chapters in `external/rexc/docs/`)

### Phase 7 — Self-hosting (P1-3)

`rexc` is rewritten in rexy. This is the language's adulthood marker.

### Phase 8+ — Stretch / P2

- Borrow checker (still a maybe — see Risk R-7)
- Macros (still a maybe — comptime may obsolete the need)
- Package manager
- Formatter, linter
- Windows target

---

## 8. Risks & Mitigations

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| **R-1** | **"Best of both" framing fails in market.** Every language that pitched "Rust + C++" got read as "neither." | High | High | Reframe externally as "Frictionless Concurrency" or "Async-first systems language." Anchor success to Drunix, not external adoption. Treat ecosystem traction as P2. |
| **R-2** | **Async + reflection designed before structs collapse under their own weight.** | High | Critical | Sequence honored in §7: types → generics → async → reflection. Each phase has an exit criterion. |
| **R-3** | **Diagnostics mandate becomes lip service.** Every language wants good errors; few deliver. | Medium | High | FR-024 caps diagnostic length. FR-025 forbids instantiation noise. Diagnostics are a code-review gate; degraded errors block the PR. |
| **R-4** | **Cross-compile from macOS is a 6-month rabbit hole.** Ship a linker, sysroot, libc stubs. | High | Medium | Phase 5 is its own track. Initial path: vendor `lld` + a minimal Drunix-shaped sysroot. Rust's `rustup target add` is the model. |
| **R-5** | **Pluggable executor drifts into a viral runtime.** "Default executor" becomes "the executor." | Medium | High | Stdlib executor and freestanding executor live in separate modules. Drunix uses the freestanding one from day one to keep the abstraction honest. |
| **R-6** | **No borrow checker means the safe subset has unsafe escape valves we didn't catch.** | Medium | High | FR-019 enumerates the safe-subset rules. Anything not on that list is `unsafe`-only. Periodic audit of `unsafe` use in stdlib. |
| **R-7** | **Borrow checker pressure returns later.** Users demand it; adding it post-v1 is harder than designing for it. | Medium | Medium | Acknowledged, deferred. Track as an open question; do not commit to "never." Vale-style generational references and Cyclone-style region annotations stay on the table. |
| **R-8** | **No C++ interop kills C++ persona adoption.** | High | Medium | Accept it. C interop is the bridge; C++ is non-goal NG-3. C++ engineers who switch are the ones who already wanted to leave C++. |
| **R-9** | **LSP is missing for too long.** Adoption blocker #2 in the market analysis. | High | High | LSP is Phase 6 but should be prototyped earlier, even crudely. A bad LSP beats no LSP. |
| **R-10** | **Stdlib void.** No async-native stdlib, users go back to Go/Rust. | High | High | Stdlib is co-developed with Phases 2–4. `Vec`, `HashMap`, `Option`, `Result`, async I/O ship with the language, not as third-party. |
| **R-11** | **Solo-team velocity ceiling.** A single-author systems language hits Zig's ceiling before mass adoption. | High | Low (matches stated ambitions) | Honest scoping: success ceiling is "respected alternative + Drunix runs on it." Mass adoption is not a P0 metric. |
| **R-12** | **No package manager pushes users to ad-hoc git submodule conventions.** | Medium | Medium | NG-4 stands for v1. Document the recommended directory layout. Re-evaluate at Phase 7. |

---

## 9. Open Questions

These need resolution before each phase begins. They are deliberately *not* answered in this PRD.

- **OQ-1** Does rexy adopt Rust-style traits, Go-style structural interfaces, Zig-style implicit comptime contracts, or Haskell/Swift-style typeclasses? (Phase 2 entry decision.)
- **OQ-2** Is async colored or colorless? PRD recommends colored (FR-009) but the technical research flagged this as the riskiest sub-decision. (Phase 4 entry decision.)
- **OQ-3** Reflection: is `comptime` a separate construct (Zig) or are types first-class values everywhere (Jai)? (Phase 3 entry decision.)
- **OQ-4** Memory: does rexy add region/lifetime annotations as syntactic sugar even without a full borrow checker? Or stay strictly Zig-shaped? (Phase 2 entry decision.)
- **OQ-5** What is the canonical exception/panic model for unrecoverable errors? Abort? Process-level panic? Per-task isolation? (Phase 4 entry decision; interacts with async cancellation.)
- **OQ-6** Build system: rexy-native (`rexc build`), CMake, or something simpler? (Phase 5/6.)

---

## 10. Adversarial Review Summary

This PRD draft was challenged by a Sonnet adversary acting as a skeptical reviewer plus a market-landscape analyst. Material findings absorbed into the document:

- **"Best of both" framing rejected.** §1 reframes the pitch as "Frictionless Concurrency." (Was originally "Rust + C++ best of both.")
- **Sequencing constraint added.** §7 orders types → generics → async → reflection. (R-2.)
- **Cross-compile is a distribution promise.** §3 P0-7 and FR-020 promote toolchain manifest from a side note to a P0 goal.
- **Stdlib void is a top adoption blocker.** R-10 makes stdlib co-developed with phases, not deferred.
- **LSP is adoption blocker #2.** R-9 acknowledges and pulls LSP earlier than original Phase 6 wanted.
- **C++ interop demoted to non-goal.** NG-3 makes the trade-off explicit; Persona B is told "we won't help you with the C++ legacy bridge."
- **Honest ceiling acknowledged.** R-11 names the realistic outcome ("respected alternative + Drunix runs on it"), not "mass adoption."

Adversarial review: applied (providers: Sonnet for technical, Gemini for market — Codex unavailable, model ID error).

---

## 11. Self-Score (100-point AI-optimization framework)

| Category | Max | Score | Notes |
|---|---|---|---|
| **AI-Specific Optimization** | 25 | 22 | FRs are numbered and traceable to goals. Phases have explicit exit criteria. Non-goals enumerated. Could improve by adding a machine-readable manifest (YAML frontmatter listing goals → FRs). |
| **Traditional PRD Core** | 25 | 23 | Problem, goals, metrics, personas, non-goals, risks all present. Personas are concrete with named pain points. |
| **Implementation Clarity** | 30 | 26 | Phases are dependency-ordered and justified. Each phase has an exit criterion. Could improve with sub-phase milestones and effort estimates per FR. |
| **Completeness** | 20 | 17 | Open questions called out (§9). Adversarial review applied (§10). Missing: detailed competitor SWOT table, formal stakeholder list (this is a one-author project, so reduced impact). |
| **Total** | **100** | **88** | Solid v1 PRD. Targeted gaps for v2: per-FR effort estimates, competitor SWOT depth, traceability YAML. |

---

## 12. What This PRD Enables Next

This PRD is the foundation for a separate **feature roadmap** doc (`external/rexc/docs/roadmap.md` — to be written) that will:

- Take the FRs in §6 and break each into per-feature P0/P1/P2 entries
- Add stdlib API surface (Vec, HashMap, async I/O, etc.) as concrete features
- Add effort estimates per feature
- Cross-reference grammar gaps in `grammar/Rexy.g4` and current `examples/*.rx`

The roadmap is the next step. Run `/octo:embrace "build a P0/P1/P2 feature roadmap for the rexc compiler covering language features and stdlib, using docs/prd.md as the foundation"` when ready.
