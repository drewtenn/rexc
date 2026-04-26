# Rexc Layered Standard Library Design

## Purpose

Rexc needs a small standard library path that feels familiar to Rust and C++
without forcing the language to grow modules, imports, generics, ownership, or
heap allocation all at once. The first milestone should make normal command-line
programs useful: write text, read one line of input, and link those facilities
automatically when Rexc builds an executable.

The standard library will be layered after Rust's `core` / `alloc` / `std`
architecture, adapted to Rexc's current bootstrap state:

- `core`: target-independent declarations and compiler-known contracts that do
  not assume an operating system.
- `alloc`: a future allocator-backed layer for owned strings, vectors, boxes,
  and other heap types once Rexc has an allocator contract.
- `std`: hosted facilities built on top of `core` and, later, `alloc`,
  available for normal command-line executable builds.
- `sys`/target runtime adapters: small target- and OS-specific implementations
  that bridge `std` calls to Darwin, Linux-compatible ELF, Drunix, or other
  platform interfaces.

The current target assembly strings are a bootstrap mechanism, not the stdlib
implementation strategy. They should provide only narrow ABI/syscall hooks
while portable library behavior lives in Rexc source. If Rexc cannot express a
needed stdlib operation yet, that missing language or compiler feature is the
next task; do not reimplement the operation in per-target assembly to sidestep
the blocker.

## First Milestone

The first user-facing API is a prelude-style surface. Programs can call these
functions directly without module path syntax:

```rust
fn main() -> i32 {
    println("name?");
    let name: str = read_line();
    print("hello ");
    println(name);
    return 0;
}
```

Initial prelude functions:

| Function | Type | Behavior |
| --- | --- | --- |
| `print` | `fn(str) -> i32` | Writes a null-terminated string to stdout without adding a newline. Returns bytes written or a negative error code. |
| `println` | `fn(str) -> i32` | Writes a string, then writes `\n`. Returns bytes written including the newline or a negative error code. |
| `read_line` | `fn() -> str` | Reads from stdin into a Rexc-owned static null-terminated buffer and returns it. The buffer is overwritten by the next `read_line` call. |
| `exit` | `fn(i32) -> i32` | Terminates the process with the given status. The return type is `i32` only because Rexc does not have `void` or never types yet. |

`read_line` uses a fixed-size `static mut [u8; 1024]` buffer in Rexc source.
It calls the target `sys_read` primitive, strips one trailing newline when
present, and always null-terminates the buffer. If reading fails or reaches EOF
before reading bytes, it returns an empty string.

## Language Surface

The current grammar only allows calls inside expressions, which makes
`println("hello");` invalid as a statement. This milestone adds expression
statements for function calls:

```rust
println("hello");
exit(1);
```

The parser should accept call expression statements only. General expression
statements such as `1 + 2;` are not useful yet and should stay invalid to avoid
silent ignored values.

The stdlib is allowed to drive small language additions when they remove
portable behavior from `sys`. The first examples are:

- `str[index]` byte indexing, so string algorithms can inspect bytes in Rexc;
- `static mut NAME: [u8; N];`, so `std` can own fixed buffers in Rexc source;
- indirect assignment through arbitrary address expressions, so Rexc can write
  terminators into those buffers with `*(buffer + index) = 0`.

Future stdlib work should follow the same rule: implement the missing Rexc
feature first, then implement the library behavior in Rexc.

The semantic analyzer should treat prelude standard functions as module-level
function declarations that exist before user functions are checked. User code
must not define another function with one of these names in the same prelude
scope; duplicate definitions should produce the same duplicate-function
diagnostic shape as user-defined functions.

## Compiler Architecture

Add a small standard-library description module in the compiler, separate from
parsing, semantic analysis, and target code generation. In the first milestone
it should provide:

- names and signatures for prelude functions;
- runtime symbol names for each prelude function;
- target-specific runtime assembly text for hosted executable links.

The frontend uses the declarations during semantic analysis. IR lowering keeps
calls as normal `ir::CallValue` nodes. Backends do not need special call nodes;
they only need a symbol-name hook so a source call to `println` can become the
runtime symbol for the selected target.

The first implementation can keep source-level names and runtime symbols equal
on ELF (`println`, `print`, `read_line`, `exit`) and use the existing Darwin
symbol prefixing path for Mach-O (`_println`, `_print`, `_read_line`, `_exit`).
If user-defined names later need separate namespaces, the runtime symbol table
can move to reserved names such as `__rexc_std_print`.

After this bootstrap milestone, the compiler metadata should be only a catalog
of library items. The implementation home for portable library behavior is
Rexc source compiled into the hosted runtime object. Portable functions such as
byte-string comparisons, integer formatting, parsing, line reading, and
eventually collection helpers must be implemented once above the target runtime
adapters. Target-specific files are reserved for calling conventions, symbol
naming, syscalls, libc shims, allocator hooks, and process startup/termination
details.

The current compiler should reflect that direction in its source layout:

- `include/rexc/stdlib.hpp`: public compiler-facing catalog API;
- `src/stdlib/core/`: target-independent catalog entries;
- `src/stdlib/std/`: hosted prelude catalog entries;
- `src/stdlib/stdlib.cpp`: aggregate catalog facade and bootstrap compiler for
  Rexc stdlib source;
- `src/stdlib/sys/`: target runtime adapters for primitive host hooks only.

Early catalog entries should record whether an item belongs conceptually to
`core`, future `alloc`, hosted `std`, or target `sys`. During bootstrap, if a
`core` or `std` function seems to require target assembly, treat that as a Rexc
capability gap and close the gap in the compiler or language before
continuing.

Early `core` memory helpers such as byte fill, byte copy, and bounded
string-to-buffer copy may use raw `*u8` pointers. They are stepping stones
toward `alloc`, not a substitute for owned strings, slices, or vectors.

## Runtime And Linking

Executable links should automatically include the hosted `std` runtime object
for the selected target:

- `arm64-macos`: assemble a generated Darwin ARM64 runtime object and link it
  with `clang -arch arm64` alongside the program object.
- `i386` ELF on macOS cross builds: assemble an i386 runtime object with
  `x86_64-elf-as --32` and link it with `x86_64-elf-ld`.
- `x86_64` ELF on macOS cross builds: assemble an x86_64 runtime object with
  `x86_64-elf-as --64` and link it with `x86_64-elf-ld`.
- Linux-style host x86 executable links: include the runtime object in the
  `clang` or `cc` link command.

Assembly-only (`-S`) and object-only (`-c`) builds may reference standard
library symbols but do not link the runtime. Documentation should explain that
final links are responsible for providing those symbols when users manually
assemble or link.

Drunix `--drunix-root` linking is out of scope for the first hosted `std`
adapter. Drunix should eventually get its own runtime adapter that calls the
Drunix user syscall or libc layer.

## Target Runtime Contracts

All targets use the existing target calling conventions:

- i386: cdecl, first argument at `8(%ebp)`, return in `%eax`;
- x86_64: System V, first argument in `%rdi`, return in `%rax`;
- arm64-macos: Apple ARM64, first argument in `x0`, return in `x0`.

String values are null-terminated byte strings. String literals already emit
`.asciz` payloads, so `print` and `println` can compute length by scanning for
the first `0` byte.

For ELF freestanding executables, the runtime should use Linux-compatible
syscalls:

- i386: `int $0x80`, `read = 3`, `write = 4`, `exit = 1`;
- x86_64: `syscall`, `read = 0`, `write = 1`, `exit = 60`.

For Darwin ARM64 executables, `sys_write` and `sys_read` call libc `write` and
`read` through `clang` linking. `sys_exit` uses the primitive Darwin exit
syscall directly so Rexc's public `exit` wrapper does not recursively call
itself.

## Testing

Add tests at three levels:

1. Parser/frontend tests for call statements:

```rust
fn main() -> i32 {
    println("hello");
    return 0;
}
```

2. Semantic tests proving prelude functions type-check without `extern fn`
   declarations, reject wrong argument counts/types, and reject user functions
   that duplicate prelude names.

3. Link smoke tests that build and run, where runnable:

```rust
fn main() -> i32 {
    println("hello");
    return 0;
}
```

On Apple Silicon macOS, the default Darwin executable should run and print:

```text
hello
```

ELF cross-compiled executables should be checked with `file`; running them on
macOS remains out of scope.

## Documentation

The README should gain a `Standard Library` section that documents:

- the layered `core`/`std` model;
- the initial prelude functions;
- fixed-buffer behavior for `read_line`;
- the difference between executable builds, which link `std`, and `-S`/`-c`
  builds, which only reference runtime symbols.

## Non-Goals

- Module path syntax such as `std::io::println`.
- Imports or package management.
- Heap allocation, owned strings, slices, vectors, or formatting macros.
- Generic traits such as Rust `Display` or C++ stream overloads.
- Drunix `std` adapter support.
- A stable ABI promise for the prelude function names.

## Rust-Style Roadmap

### Critical Rule: Rexc First

The standard library should be implemented in portable Rexc by default. This
is not optional polish; it is the roadmap's main constraint. Code should split
by target only at the lowest host or hardware boundary, such as `read`,
`write`, `exit`, and future allocation primitives. `sys` exists to expose those
primitive effects, not to become a parallel standard library written in
assembly.

When stdlib work hits a missing Rexc capability, the next step is to implement
that capability in Rexc's compiler, IR, or backends, then continue the library
work in Rexc source. Examples:

- Need to inspect string bytes? Add `str[index]`, then write `strlen` and
  `str_eq` in Rexc.
- Need a line buffer? Add `static mut [u8; N]`, global buffer references, and
  byte stores, then write `read_line` in Rexc.
- Need owned strings or vectors? Add the allocator contract and ownership
  model required for `alloc`, then implement the collection in Rexc.
- Need errors? Add the needed enum/result representation, then replace
  sentinel return values with typed results.

Canonical stdlib source files should be `.rx` files. C++ files in
`src/stdlib/` may describe the catalog, load/embed those `.rx` sources, compile
them into the hosted runtime object, and provide primitive `sys` adapters.
They must not be the long-term home for portable stdlib implementation text.
An embedded C++ string such as `portable_stdlib_source()` is a temporary
bootstrap bridge only; the roadmap is to move that text into real
`src/stdlib/core/*.rx` and `src/stdlib/std/*.rx` files.

Rexc should grow the standard library in layers rather than by adding every new
function to every architecture-specific assembly file.

### Stage 1: Rexc-Compiled Hosted Runtime

Keep the current prelude and generated runtime object model, but compile the
portable implementation from Rexc source into that runtime object. This gives
Rexc working command-line programs while the language is still missing modules,
imports, ownership, heap allocation, traits, slices, and result types. The
constraint is strict: new assembly is acceptable only for the lowest
target-specific primitive hooks.

### Stage 2: Target Triple Runtime Adapters

Split runtime support by target triple instead of only CPU family:

- `i386-linux` or `i386-elf` for Linux-compatible `int $0x80` paths;
- `x86_64-linux` for Linux `syscall` paths;
- `arm64-macos` for Darwin symbol names and libc calls;
- `i386-drunix` for Drunix userland once that adapter is ready.

These adapters should expose a tiny internal ABI: read bytes, write bytes,
exit, and later allocate/deallocate. Their job is to isolate platform and
hardware details at the lowest possible level, not to duplicate portable
stdlib algorithms. `read_line`, string scanning, integer parsing, formatting,
buffer management, and similar behavior must remain above this layer in Rexc.

### Stage 3: Portable `core`

Move target-independent contracts into a real `core` library surface. Early
`core` candidates include primitive type operations, byte-string/slice
contracts, null/length conventions, memory intrinsics, and abort/panic hooks.
This layer must not assume an OS or heap.

### Stage 4: Portable Library Implementations

Implement reusable library logic once above the runtime adapters. String
length, equality, emptiness, prefix/suffix/search helpers, integer formatting,
integer parsing, line reading, and later formatting building blocks should live
here instead of being rewritten for i386, x86_64, and ARM64. The implementation
language is Rexc source. If that source cannot yet express the operation,
implement the missing Rexc capability first.

### Stage 5: Add `alloc`

Once Rexc has allocator hooks and ownership rules, introduce an `alloc` layer
for heap-backed types such as owned strings, vectors, and boxed values. This
layer should depend on `core` plus an allocator interface, but not on files,
terminals, environment variables, or process services.

During bootstrap, `alloc` may start as a portable bump arena implemented in
Rexc source with `static mut` byte storage and scalar offset state. That is an
allocator contract seed, not the final ownership model for vectors or strings.
Allocation-backed string helpers may return `str` views into that arena until
Rexc has owned string and lifetime semantics.

### Stage 6: Grow Hosted `std`

Move hosted functionality into `std`: console I/O, process arguments,
environment variables, files, paths, exit status, and eventually richer
formatting. When module syntax exists, migrate the user-facing API toward paths
such as `std::io::println`, with a small prelude preserving convenient short
names where appropriate.
