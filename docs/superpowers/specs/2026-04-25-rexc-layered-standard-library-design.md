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

The current target assembly strings are a bootstrap mechanism, not the desired
long-term stdlib shape. They should provide narrow ABI/syscall hooks while
portable library behavior moves into Rexc library source or another shared
runtime form.

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
| `read_line` | `fn() -> str` | Reads from stdin into a runtime-owned null-terminated buffer and returns it. The buffer is overwritten by the next `read_line` call. |
| `exit` | `fn(i32) -> i32` | Terminates the process with the given status. The return type is `i32` only because Rexc does not have `void` or never types yet. |

`read_line` uses a fixed-size runtime buffer of 1024 bytes in this milestone.
It strips one trailing newline when present and always null-terminates the
buffer. If reading fails or reaches EOF before reading bytes, it returns an
empty string.

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

After this bootstrap milestone, the compiler metadata should become a catalog
of library items rather than the implementation home for the library. Portable
functions such as byte-string comparisons, integer formatting, parsing, and
eventually collection helpers should be implemented once above the target
runtime adapters. Target-specific files should be reserved for calling
conventions, symbol naming, syscalls, libc shims, allocator hooks, and process
startup/termination details.

The current compiler should reflect that direction in its source layout:

- `include/rexc/stdlib.hpp`: public compiler-facing catalog API;
- `src/stdlib/core/`: target-independent catalog entries;
- `src/stdlib/std/`: hosted prelude catalog entries;
- `src/stdlib/stdlib.cpp`: aggregate catalog facade used by the compiler;
- `src/stdlib/sys/`: target runtime adapters and bootstrap assembly hooks.

Early catalog entries should record whether an item belongs conceptually to
`core`, future `alloc`, hosted `std`, or target `sys`. During bootstrap, some
`core`-style functions may still be emitted through target runtime objects, but
that should be treated as an implementation limitation rather than the desired
library boundary.

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

For Darwin ARM64 executables, the runtime can call libc functions (`write`,
`read`, `_exit`) through `clang` linking, or use Darwin syscalls directly. The
preferred first implementation is libc calls because the executable already
links through `clang -arch arm64` and that keeps syscall numbering out of the
compiler.

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

## Rust-Like Roadmap

Rexc should grow the standard library in layers rather than by adding every new
function to every architecture-specific assembly file.

### Stage 1: Bootstrap Hosted Runtime

Keep the current prelude and generated runtime object model for now. This gives
Rexc working command-line programs while the language is still missing modules,
imports, ownership, heap allocation, traits, slices, and result types. The
important constraint is that new assembly routines should be treated as
temporary bootstrap hooks unless they are genuinely target-specific.

### Stage 2: Target Triple Runtime Adapters

Split runtime support by target triple instead of only CPU family:

- `i386-linux` or `i386-elf` for Linux-compatible `int $0x80` paths;
- `x86_64-linux` for Linux `syscall` paths;
- `arm64-macos` for Darwin symbol names and libc calls;
- `i386-drunix` for Drunix userland once that adapter is ready.

These adapters should expose a small internal ABI: read bytes, write bytes,
exit, and later allocate/deallocate. Their job is to isolate platform details,
not to duplicate portable stdlib algorithms.

### Stage 3: Portable `core`

Move target-independent contracts into a real `core` library surface. Early
`core` candidates include primitive type operations, byte-string/slice
contracts, null/length conventions, memory intrinsics, and abort/panic hooks.
This layer must not assume an OS or heap.

### Stage 4: Portable Library Implementations

Implement reusable library logic once above the runtime adapters. String
length/equality, integer formatting, integer parsing, and later formatting
building blocks should live here instead of being rewritten for i386, x86_64,
and ARM64. The implementation language can start as compiler-emitted IR or
portable runtime source, then move into Rexc source as the language becomes
self-hosting enough.

### Stage 5: Add `alloc`

Once Rexc has allocator hooks and ownership rules, introduce an `alloc` layer
for heap-backed types such as owned strings, vectors, and boxed values. This
layer should depend on `core` plus an allocator interface, but not on files,
terminals, environment variables, or process services.

### Stage 6: Grow Hosted `std`

Move hosted functionality into `std`: console I/O, process arguments,
environment variables, files, paths, exit status, and eventually richer
formatting. When module syntax exists, migrate the user-facing API toward paths
such as `std::io::println`, with a small prelude preserving convenient short
names where appropriate.
