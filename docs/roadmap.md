# Rexc Compiler Roadmap

Updated: 2026-04-26

This roadmap tracks Rexc as a portable compiler first, not only as a
standard-library design or a Drunix-specific tool. Rexc should build and run as
a normal host compiler on macOS, Linux, and Windows while still producing
useful systems programs for Drunix and other explicit output targets.

The stages below are grouped by state:

- **Completed phases** are implemented enough to build on, though individual
  details may still need polish.
- **Active phase** is the work currently being wired through the compiler.
- **Future phases** describe the next durable compiler capabilities in a likely
  order. They are milestones, not fixed release promises.

## Guiding Rules

- Keep the pipeline explicit: source text becomes an AST, the AST becomes typed
  IR, typed IR becomes target assembly, and assembly becomes an object or
  executable.
- Keep host portability first: core compiler code should stay OS-neutral, with
  host-specific filesystem, process, toolchain, assembler, and linker behavior
  isolated behind small boundaries.
- Keep Drunix visible as a target: target and runtime choices should continue
  to explain how a Rexc program becomes a Drunix user program.
- Keep portable behavior in Rexc source whenever the language can express it.
  Target-specific C++ or assembly should stay at ABI, syscall, libc, startup,
  and linker boundaries.
- Add compiler features when the standard library needs them instead of copying
  portable library behavior into every target runtime adapter.
- Land each stage with parser, semantic, IR, backend, CLI, and smoke coverage
  appropriate to the surface it changes.

## Current Snapshot

Rexc's host portability goal is macOS, Linux, and Windows. The compiler
currently supports a small but complete output pipeline:

```text
source .rx
  -> ANTLR lexer and parser
  -> AST
  -> semantic analysis
  -> typed IR
  -> target assembly
  -> assembler object
  -> hosted or Drunix executable link
```

The language currently includes functions, extern functions, primitive scalar
types, pointers, string literals, locals, mutable locals, assignments, indirect
pointer stores, calls as expressions and statements, `return`, `if/else`,
`while`, `break`, `continue`, arithmetic, comparisons, boolean operators,
explicit casts, pointer arithmetic, indexing, static buffers and scalars,
inline modules, file-backed modules, package search paths, `use` imports, and
qualified calls.

The currently implemented output target families are:

- `i386-linux` / `i386-elf`
- `i386-drunix`
- `x86_64-linux` / `x86_64-elf`
- `arm64-macos`

Executable builds can include a hosted runtime and the Rexc-source standard
library. Drunix builds use the `i386-drunix` runtime path with `--drunix-root`
and link against Drunix startup objects, runtime archive, and linker script.

## Completed Phases

### Phase 0: Project Skeleton, Build, And Test Harness

Status: completed.

Rexc has a CMake build, generated ANTLR parser, reusable `rexc_core` library,
CLI executable, unit tests, parser architecture tests, CLI smoke tests, assemble
smoke tests, examples, and book/documentation build support.

Delivered:

- compiler library and CLI split;
- pinned ANTLR tool/runtime generation;
- `ctest` integration;
- smoke examples in `examples/`;
- book sources in `docs/`.

Exit criteria already met:

- `cmake --build build` produces the compiler and tests;
- `ctest --test-dir build --output-on-failure` exercises the core pipeline and
  available smoke workflows.

### Phase 1: Source Text To AST

Status: completed for the current language surface.

The parser accepts top-level items, functions, extern declarations, static
buffers/scalars, statements, expressions, primitive types, pointer types,
literals, module structure, imports, and qualified call names. It preserves
source locations for diagnostics and converts the ANTLR parse tree into the
AST in `include/rexc/ast.hpp`.

Delivered:

- grammar in `grammar/Rexc.g4`;
- parser entry points in `include/rexc/parse.hpp` and `src/parse.cpp`;
- AST data model for source-level modules, declarations, statements, and
  expressions.

Remaining polish:

- keep parser diagnostics direct and source-oriented as grammar complexity
  grows;
- avoid letting grammar support get ahead of semantic and backend support.

### Phase 2: Semantic Analysis And Type Model

Status: completed for primitive Rexc programs.

Semantic analysis validates names, duplicate declarations, function calls,
return types, initializer and assignment types, mutability, pointer operations,
arithmetic, comparisons, logical operators, cast rules, loop-only control flow,
and integer literal ranges.

Delivered:

- primitive type model for signed integers, unsigned integers, `bool`, `char`,
  `str`, and pointers;
- target-aware constraints for values that parse and type-check but cannot be
  emitted by every backend;
- diagnostics for invalid frontend programs.

Remaining polish:

- improve diagnostic recovery so one syntax or type error does not hide nearby
  actionable errors;
- make future module visibility and import errors as clear as local name
  errors.

### Phase 3: Typed IR Lowering

Status: completed for the current frontend.

Checked AST modules lower into a smaller typed IR. The backend receives
resolved primitive types, lowered control flow, validated assignments, typed
calls, and target-independent operation nodes instead of raw parser structure.

Delivered:

- IR model in `include/rexc/ir.hpp`;
- lowering in `src/lower_ir.cpp`;
- tests for arithmetic, calls, control flow, casts, statics, pointer operations,
  module symbol names, and stdlib calls.

Remaining polish:

- keep IR independent from textual source conveniences;
- add explicit IR forms as richer data types, modules, and aggregate operations
  arrive.

### Phase 4: x86 Assembly, Objects, And Executables

Status: completed for current x86 targets.

Rexc emits GNU-style assembly for `i386` and `x86_64`, can assemble object
files, and can link Linux-compatible ELF executables. The Drunix path can link
an `i386-drunix` executable using Drunix userland startup and runtime inputs.

Delivered:

- i386 code generation for the supported 32-bit scalar and pointer surface;
- x86_64 code generation, including 64-bit integer support;
- signed and unsigned arithmetic/comparison behavior;
- stack frames, calls, branches, loops, string data, pointer addressing,
  indirect stores, and static data;
- `-S`, `-c`, and executable output modes;
- Drunix link mode through `--drunix-root`.

Remaining polish:

- add more ABI stress coverage around calls, returns, and mixed-width values;
- keep Drunix integration aligned with Drunix userland runtime changes;
- decide whether Rexc should eventually write object files directly or keep
  using external assemblers.

### Phase 5: Darwin ARM64 Backend

Status: completed for native macOS command-line programs.

Rexc supports the `arm64-macos` target, emits Darwin ARM64 assembly, assembles
with Apple `as`, links with the host toolchain, and can build a native
command-line executable on Apple Silicon macOS.

Delivered:

- ARM64 code generation path;
- Darwin symbol and calling-convention handling;
- native macOS default target selection on Apple Silicon;
- macOS release preset and packaging flow.

Remaining polish:

- broaden ARM64 backend parity tests against the x86 backends;
- decide whether ARM64 should grow Linux or Drunix target variants.

### Phase 6: Rexc-Source Standard Library Bootstrap

Status: completed as a bootstrap, not as a final library design.

The standard library now has `core`, `alloc`, `std`, and narrow `sys` runtime
layers. Portable library behavior lives in `.rx` files, compiler-facing
function metadata is derived from embedded Rexc source, and hosted executable
builds compile the portable stdlib into the runtime object.

Delivered:

- `src/stdlib/core/*.rx` for string, memory, number, slice-shaped, and
  sentinel-result helpers;
- `src/stdlib/alloc/alloc.rx` with a static bump arena, string helpers,
  primitive formatting, boxed `i32`, and vector-shaped `i32` helpers;
- `src/stdlib/std/*.rx` for console I/O, process/env helpers, file primitives,
  and path helpers;
- target adapters for i386 Linux-like, i386 Drunix, x86_64 Linux-like, and
  Darwin ARM64 runtime hooks;
- stdlib source embedding through CMake;
- source-derived prelude function signatures;
- qualified stdlib paths such as `std::io::println`.

Known limits:

- the prelude is intentionally too broad;
- `alloc` is a static bump arena with no deallocation;
- errors are integer sentinels instead of typed `Result` values;
- stdlib source is embedded at build time instead of loaded through the module
  system;
- `std_*` symbol names are compiler bridges, not a stable ABI.

### Phase 7: Real Module Loading

Status: completed for user modules.

Rexc has moved beyond inline-only modules. The parser and CLI can load
file-backed modules from an entry file directory and from `--package-path`
roots, then merge those parsed modules into one checked program.

Delivered:

- `mod name { ... }` continues to support inline modules;
- `mod name;` loads a sibling module file or directory module;
- `pub` controls which items can cross module boundaries;
- `use path::to::item;` resolves through loaded module trees;
- the CLI can parse a package or source tree from an entry file;
- semantic analysis reports unresolved, private, duplicate, and ambiguous
  imports with useful source locations.

Known limits:

- stdlib `.rx` files are still embedded by CMake rather than loaded through
  ordinary module search;
- dependency-cycle diagnostics and package policy still need more polish.

## Active Phase

### Phase 8: Module Policy, Packages, And Public API Shape

Status: in progress.

Goal: make Rexc programs scale beyond single files.

Planned work:

- finalize public/private item rules; **done for the current module model**
- decide the default prelude policy and which names remain always available;
- add clearer namespace rules for stdlib paths and user modules;
- refine package search roots and command-line behavior;
- add diagnostics for dependency cycles and conflicting exports;
- keep the README and book aligned with the module model.

Exit criteria:

- a multi-file Rexc program can compile without custom compiler glue;
- stdlib imports look like ordinary Rexc imports;
- private items stay private across module boundaries;
- package/module failures produce actionable diagnostics.

## Future Phases

### Phase 9: Rich Data Representation

Goal: give Rexc enough data shapes to replace bootstrap library conventions.

Planned work:

- real array and slice types instead of helper pairs like pointer plus length;
- structs with field access and field assignment;
- enums and tagged values;
- typed `Option` and `Result`;
- richer lvalue handling for locals, dereferences, indexes, and fields;
- ABI and IR rules for aggregates across all supported targets.

Exit criteria:

- stdlib helpers stop using integer sentinels for ordinary fallible operations;
- string, file, parse, env, and allocation APIs can return typed results;
- aggregate values lower through IR and emit correctly on x86 and ARM64.

### Phase 10: Allocation, Ownership, And Collections

Goal: move `alloc` from a bootstrap arena into a deliberate library layer.

Planned work:

- allocator hooks in the target runtime adapters;
- a stable allocation contract for hosted targets and Drunix;
- owned strings backed by allocator-managed storage;
- boxes and vectors that are not hard-coded to `i32`;
- ownership and lifetime rules, or another explicit resource model, before
  deallocation becomes user-visible;
- out-of-memory behavior expressed with typed results or panic contracts.

Exit criteria:

- `alloc` can be implemented mostly in Rexc source on top of target allocator
  primitives;
- owned strings and vectors have tests that cover allocation, mutation,
  passing, returning, and failure behavior;
- target adapters expose allocation primitives without absorbing portable
  collection logic.

### Phase 11: Generics, Traits, And Formatting

Goal: remove type-specific duplication once the core data model is strong
enough.

Planned work:

- choose a minimal generic function/type model;
- add trait-like contracts only when there is a concrete use in the stdlib;
- generalize `box_i32`, `vec_i32`, parsing, and formatting helpers;
- replace one-off print helpers with a principled formatting story;
- keep monomorphization or dynamic dispatch choices visible in IR and backend
  tests.

Exit criteria:

- library authors can write one vector or formatting implementation instead of
  one per primitive type;
- generated symbols and diagnostics remain understandable;
- compile-time costs and emitted code size stay observable in tests.

### Phase 12: Host Portability, Runtime, And Target Maturity

Goal: make Rexc reliable as a compiler host on macOS, Linux, and Windows while
keeping output targets explicit and well tested.

Planned work:

- build and test the compiler as a normal host tool on macOS, Linux, and
  Windows;
- isolate path handling, process execution, temporary files, environment
  access, and toolchain discovery behind portable host abstractions;
- document dependency setup for all three major host operating systems;
- fill Drunix command-line argument support;
- add x86_64 Drunix runtime/link support when Drunix userland has matching
  startup and ABI pieces;
- keep host support separate from output target support, and keep Linux-like,
  ELF, Drunix, and Darwin target triples explicit;
- grow file, environment, process, and path behavior through portable stdlib
  code over narrow `sys` hooks;
- improve temporary-file and linker-driver behavior in the CLI;
- evaluate direct object emission after the assembly path is well covered.

Exit criteria:

- macOS, Linux, and Windows host builds are documented and covered by CI or an
  equivalent repeatable verification path;
- each supported target has documented assemble, object, executable, and stdlib
  expectations;
- Drunix-linked examples run through the same high-level CLI flow as hosted
  examples;
- runtime adapters stay small and target-specific.

### Phase 13: Diagnostics, Tooling, And Developer Experience

Goal: make Rexc easier to use, debug, and evolve.

Planned work:

- structured diagnostics with ranges, notes, and suggestions;
- direct stdlib parse/sema/codegen diagnostics instead of hidden embedded
  runtime failure text;
- richer CLI help and target reporting;
- VS Code extension improvements for grammar highlighting and diagnostics;
- formatter or style checker once the language syntax stabilizes;
- expanded examples and book chapters for modules, stdlib, and Drunix linking.

Exit criteria:

- common user mistakes point to the source span that caused them;
- failed stdlib builds read like compiler errors, not backend artifacts;
- examples cover the normal path for new Rexc users.

### Phase 14: Optimization And Backend Architecture

Goal: keep emitted code simple now, then optimize once semantics are stronger.

Planned work:

- define which optimizations belong in IR versus target backends;
- add simple constant folding and dead-code cleanup when it improves clarity;
- preserve debugability for teaching and Drunix integration;
- add backend conformance tests shared across x86 and ARM64;
- consider register allocation improvements only after richer programs demand
  them.

Exit criteria:

- optimizations are tested as semantic-preserving transformations;
- backend differences remain intentional and documented;
- generated assembly stays inspectable enough for the project goals.

### Phase 15: Self-Hosting Horizon

Goal: make Rexc capable enough to implement substantial parts of its own
tooling or libraries in Rexc.

This is a long-horizon phase, not near-term work. Before it becomes practical,
Rexc needs real modules, richer data types, allocation, collections, useful
error handling, file I/O, and enough language ergonomics to write medium-sized
programs without fighting the compiler.

Possible milestones:

- write nontrivial command-line tools in Rexc;
- move more build-time stdlib generation logic into Rexc;
- experiment with parsing or code-generation utilities written in Rexc;
- eventually evaluate whether a Rexc-in-Rexc compiler component is useful.

## Cross-Cutting Work

These items do not belong to one phase, but they should be checked whenever a
phase changes the compiler surface:

- update examples that show the intended user-facing style;
- update README and book chapters when behavior changes;
- keep `docs/roadmap.md` current after major milestones;
- run core tests and smoke tests before calling a phase complete;
- avoid documenting bootstrap names, symbols, or helper APIs as stable unless
  the project is ready to support them.
