# Rexc

Rexc is an experimental systems-language compiler intended to be a portable
host tool on macOS, Linux, and Windows first. Drunix remains an important
target and proving ground, but the compiler itself should build and run as a
normal developer tool across all major operating systems.

That means core compiler code should stay host-neutral. OS-specific behavior
belongs behind small boundaries for filesystem access, process execution,
toolchain discovery, assembler/linker invocation, runtime adapters, and target
selection.

## Prerequisites

### Core compiler build

- CMake 3.20+
- A C++17 compiler toolchain (`c++`, `clang++`, or `g++`)
- Java runtime (required for ANTLR code generation)
- Network access during configure so CMake can download:
    - `antlr-4.13.2-complete.jar`
    - `antlr4-cpp-runtime-4.13.2-source.zip`

Optional but commonly used:

- Ninja (if you configure with `-G Ninja`)

### Test and object/ELF workflows

- POSIX shell tools used by test scripts (`sh`/`bash`, `grep`)
- GNU assembler for `-c` workflows and assemble smoke tests:
    - preferred: `x86_64-elf-as`
    - fallback: system `as`
- For Drunix link flows and ELF inspection examples:
    - `x86_64-elf-ld`
    - `x86_64-elf-readelf`
    - `file` and `xxd`

### Book/docs build (`make docs`)

- `pandoc`
- `typst`
- `perl`
- `zip` and `unzip`
- `python3`
- Python package `Pillow` (used by `docs/generate_cover.py`)
- Diagram rasterizer, one of:
    - `rsvg-convert`
    - Python package `cairosvg`

### Install all dependencies by OS

macOS (Homebrew, single line):

```sh
brew install cmake ninja llvm openjdk pandoc typst perl zip unzip x86_64-elf-binutils file librsvg python && python3 -m pip install --user Pillow cairosvg
```

On macOS, Homebrew `openjdk` is not always first on `PATH` by default. If CMake
reports it cannot find Java, add:

```sh
echo 'export PATH="/opt/homebrew/opt/openjdk/bin:$PATH"' >> ~/.zprofile && echo 'export JAVA_HOME="/opt/homebrew/opt/openjdk/libexec/openjdk.jdk/Contents/Home"' >> ~/.zprofile && source ~/.zprofile
```

Or configure with an explicit Java executable (works even without changing shell startup files):

```sh
cmake -S . -B build -DJava_JAVA_EXECUTABLE="$(brew --prefix openjdk)/bin/java"
```

Ubuntu/Debian:

```sh
sudo apt update && sudo apt install -y cmake ninja-build build-essential default-jre pandoc typst perl zip unzip binutils file xxd librsvg2-bin python3 python3-pip && python3 -m pip install --user Pillow cairosvg
```

Fedora:

```sh
sudo dnf install -y cmake ninja-build gcc-c++ java-21-openjdk pandoc typst perl zip unzip binutils file vim-common librsvg2-tools python3 python3-pip && python3 -m pip install --user Pillow cairosvg
```

Windows:

Windows is a first-class host portability goal for Rexc. The exact dependency
recipe still needs to be documented as the compiler's host-toolchain boundary
is cleaned up for MSVC, clang-cl, MinGW, PowerShell, and Windows path/process
behavior.

Notes:

- CMake fetches the pinned ANTLR tool jar and ANTLR C++ runtime automatically during configure.
- `cairosvg` is optional when `rsvg-convert` is installed, but installing both is fine.

## Book

The Rexc book lives in `docs/`. It follows the same chapter-driven teaching
style as the Drunix book: each chapter narrates one stage of the compiler
pipeline and ends by describing what the compiler knows at that point.

Start with `docs/cover.md`. The canonical source order is listed in
`docs/sources.mk`.

Build the PDF and EPUB with the same Pandoc/Typst pipeline used by the Drunix
book:

```sh
make docs
```

Individual outputs can be built with `make pdf` or `make epub`. The generated
files are `docs/Rexc.pdf` and `docs/Rexc.epub`.

## Build

The CMake build requires a Java runtime for ANTLR code generation. It downloads
the pinned ANTLR tool jar and C++ runtime into the build tree during configure.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## VS Code Extension

The VS Code extension lives in `tools/vscode-rexc`. It is made of static
extension metadata, language configuration, and TextMate grammar files, so the
build validates those files and packages them into a VSIX.

Prerequisites:

- Node.js with `npx`
- VS Code, if you want to launch or install the extension locally

Build the extension from the repository root with:

```sh
cd tools/vscode-rexc
node scripts/verify-extension.mjs
npx --yes @vscode/vsce package --no-dependencies
```

The packaging command writes a `rexc-<version>.vsix` file in
`tools/vscode-rexc`. Install it from VS Code with
**Extensions: Install from VSIX...**.

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

## Compile, Assemble, And Link

```sh
build/rexc examples/add.rx -S -o build/add.s
build/rexc examples/add.rx -c -o build/add.o
build/rexc examples/add.rx -o build/add
build/rexc examples/branch.rx --target i386 -S -o build/branch32.s
build/rexc examples/add.rx --target i386 -o build/add-i386.elf
build/rexc examples/wide.rx --target x86_64 -S -o build/wide64.s
build/rexc examples/wide.rx --target x86_64 -c -o build/wide64.o
build/rexc examples/add.rx --target x86_64 -o build/add-x86_64.elf
build/rexc examples/add.rx --target arm64-macos -S -o build/add-arm64.s
build/rexc examples/add.rx --target arm64-macos -c -o build/add-arm64.o
build/rexc examples/add.rx --target arm64-macos -o build/add-arm64
build/rexc examples/add.rx --target x86_64-linux -S -o build/add-x86_64-linux.s
```

The default target is the native host target. On Apple Silicon macOS, omitting
`--target` selects `arm64-macos` and produces Darwin ARM64 assembly, Mach-O
objects, or Mach-O command-line executables. On non-Darwin hosts, the default
target remains `i386`. Use `--target` whenever you want to cross-compile.
`-S` writes assembly, while `-c` runs the target assembler and writes an object
file. Omitting `-S` and `-c` asks Rexc to produce a linked command-line
executable for the selected or default target.

The short target names remain supported, and explicit aliases such as
`i386-linux`, `i386-elf`, `i386-drunix`, `x86_64-linux`,
`x86_64-elf`, `arm64-apple-darwin`, and `aarch64-apple-darwin` preserve their
runtime target identity. The Linux and ELF aliases share Linux-compatible
runtime adapters, while `i386-drunix` selects the Drunix userland adapter.

For x86 targets on Linux-style hosts, executable linking uses the host C linker
driver (`clang` or `cc`) with `-m32` for `i386` and `-m64` for `x86_64`. On
macOS, x86 executable linking uses the cross ELF binutils pair
`x86_64-elf-as` and `x86_64-elf-ld`, generates a tiny `_start`, and writes an
ELF executable. macOS can build and inspect that ELF file, but it cannot run it
directly. Use `--target arm64-macos` when you want a native Darwin/Mach-O
command-line executable.

Examples on macOS:

```sh
build/rexc examples/add.rx -o build/add-arm64
build/rexc examples/add.rx --target i386 -o build/add-i386.elf
build/rexc examples/add.rx --target x86_64 -o build/add-x86_64.elf
file build/add-arm64 build/add-i386.elf build/add-x86_64.elf
```

### Build A Darwin arm64 Executable

On Apple Silicon macOS, Rexc can build a Mach-O `arm64` command-line
executable in one invocation:

```sh
cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-release
build/macos-arm64-release/rexc examples/add.rx \
    --target arm64-macos \
    -o build/macos-arm64-release/add-arm64
file build/macos-arm64-release/add-arm64
build/macos-arm64-release/add-arm64
echo $?
```

Expected `file` output includes:

```text
Mach-O 64-bit executable arm64
```

The final `echo $?` prints the Rexc `main` return value. For
`examples/add.rx`, that value is `42`.

Under the hood, Rexc emits temporary Darwin ARM64 assembly, assembles it with
Apple `as -arch arm64`, then links the object with `clang -arch arm64` and the
normal macOS runtime startup. Use `-S` when you only want assembly, and `-c`
when you only want the Mach-O object.

This flow creates a Darwin command-line executable, not a `.app` bundle. To put
the executable inside a minimal app bundle manually:

```sh
mkdir -p build/AddRexc.app/Contents/MacOS
cp build/macos-arm64-release/add-arm64 build/AddRexc.app/Contents/MacOS/AddRexc
cat > build/AddRexc.app/Contents/Info.plist <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>AddRexc</string>
  <key>CFBundleIdentifier</key>
  <string>dev.rexc.add</string>
  <key>CFBundleName</key>
  <string>AddRexc</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleVersion</key>
  <string>1</string>
</dict>
</plist>
PLIST
```

The current Rexc examples are console-style programs, so launching the bundle
from Finder will not show a window. For visible GUI apps, Rexc would need
bindings or runtime support for Cocoa/AppKit entry points.

To build a linked Drunix i386 executable in one compiler invocation, point Rexc
at a Drunix checkout that has `user/user.ld`, `user/lib/crt0.o`, and
`user/lib/libc.a`:

```sh
build/rexc examples/add.rx --target i386-drunix --drunix-root /path/to/DrunixOS -o build/add.drunix
```

For compatibility, `--drunix-root` also treats i386 aliases as the
`i386-drunix` runtime target. Drunix executable links include Rexc's hosted
standard-library runtime object before `user/lib/libc.a`.

## Modules And Package Paths

Use `mod foo;` to load a file-backed module named `foo`. Rexc looks for either
`foo.rx` or `foo/mod.rx`, parses that file as module `foo`, and gives its
items symbol names such as `foo_add`. Mark the module and any cross-module
items `pub` when other modules need to reach them.

```rust
// main.rx
pub mod math;
use math::add;

fn main() -> i32 {
    return add(20, 22);
}
```

```rust
// math.rx
pub fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

`use path::to::item;` imports the final item name into the current module, so
call sites can write `add(...)` instead of `math::add(...)`. Qualified calls
such as `math::add(...)` remain available without a `use`.

The entry file's directory is always searched first. Extra package roots are
searched afterward, in the order they appear on the command line:

```sh
build/rexc app/main.rx --package-path vendor/core --package-path vendor/ui -S -o build/app.s
```

Each `--package-path` value must be an existing directory. If the same module
exists in the entry directory and a package root, the entry-directory version
wins; if it exists in multiple package roots, the first matching root wins.

## Targets

Rexc's host portability goal is macOS, Linux, and Windows. Separately, the
currently implemented output targets are Linux-compatible x86 targets, a Drunix
i386 target, and a Darwin ARM64 target:

| Target | CLI option | Assembler mode | Object class | Notes |
| --- | --- | --- | --- | --- |
| `i386-linux` / `i386-elf` | `--target i386`, `--target i386-linux`, or `--target i386-elf` | `--32` | `ELF32` | Default on non-Darwin hosts; uses Linux-compatible i386 runtime hooks. |
| `i386-drunix` | `--target i386-drunix --drunix-root path` | `--32` | `ELF32` | Uses the Drunix userland runtime adapter and Drunix linker script/startup objects. |
| `x86_64-linux` / `x86_64-elf` | `--target x86_64`, `--target x86_64-linux`, or `--target x86_64-elf` | `--64` | `ELF64` | Uses the Linux/System V x86_64 calling convention and runtime hooks. |
| `arm64-macos` | omitted on macOS or `--target arm64-macos` | Apple `as -arch arm64` | Mach-O 64-bit arm64 object | Uses Darwin symbol names and Apple ARM64 calling convention. |

All targets emit assembly for functions named in the Rexc source. Final
executables still need a startup object such as `crt0.o`, a runtime library,
and a linker script or linker defaults that match the selected ABI.

## Compiler Pipeline

Rexc is organized as a small, explicit compiler pipeline. Each stage consumes
the previous stage's output and either produces the next representation or adds
diagnostics and stops.

```text
source .rx
  -> lexer and parser
  -> AST
  -> semantic analysis
  -> typed IR
  -> target assembly
  -> assembler object
  -> Linux-compatible ELF link
```

1. **CLI input**: `src/main.cpp` parses the entry file tree, including
   file-backed modules found beside the entry file or under `--package-path`
   roots, and owns the top-level flow. It prints diagnostics and exits
   non-zero if any frontend or backend stage fails.

2. **Lexing and parsing**: ANTLR generates the lexer and parser from
   `grammar/Rexc.g4`. `src/parse.cpp` invokes those generated classes for
   functions, extern declarations, immutable and mutable `let` declarations,
   assignment, indirect pointer assignment, `return`, `if/else`, `while`,
   `break`, and `continue` statements, expressions, explicit casts, type names,
   and literals, then converts the parse tree into the AST types declared in
   `include/rexc/ast.hpp`.

3. **AST**: The AST preserves source-level structure: functions, parameters,
   `let`, assignment, indirect pointer assignment, `return`, `if/else`,
   `while`, `break`, and `continue` statements, names, calls, unary/binary
   expressions, comparison expressions, logical expressions, explicit casts,
   and integer/bool/char/string literals. Integer literals keep their original
   decimal text so later stages can range-check large values without parser
   overflow.

4. **Semantic analysis**: `src/sema.cpp` validates names, duplicate functions
   and locals, function calls, return types, initializer and assignment types,
   local mutability, pointer address/dereference rules, arithmetic operands,
   comparison operands, `if` and `while` condition types, loop-only `break` and
   `continue`, unary operators, and integer literal ranges. It uses the type
   helpers in `src/types.cpp` so all frontend checks share one type model.

5. **IR lowering**: `src/lower_ir.cpp` converts the checked AST into the typed
   IR in `include/rexc/ir.hpp`. The IR carries resolved primitive types on
   functions, parameters, locals, assignments, indirect pointer assignments,
   calls, literals, unary expressions, binary expressions, comparisons,
   `if/else` branches, and `while` loops with `break` and `continue`. This
   gives the backend a smaller, typed representation to emit.

6. **x86 code generation**: `src/codegen_x86.cpp` emits GNU assembler syntax
   for either `i386` or `x86_64`. It emits supported scalar values in target
   stack slots, emits strings in `.rodata` with `.LstrN` labels, uses signed or
   unsigned division and comparison condition codes based on IR type, emits
   explicit casts with target-width sign or zero extension, emits address-of,
   dereference, scaled pointer arithmetic, pointer indexing, and indirect
   pointer stores, emits branch labels and jumps for short-circuiting logical
   operators, `if/else`, `while`, `break`, and `continue`, stores assignments
   into existing local slots, and reports backend diagnostics when a type is
   unsupported by the selected target.

7. **Assembly output**: `build/rexc input.rx [--target i386|x86_64|arm64-macos] -S -o
   output.s` writes assembly only after code generation succeeds. Failed code
   generation reports diagnostics and does not write partial assembly.

8. **Object assembly**: `build/rexc input.rx [--target i386|x86_64|arm64-macos] -c -o
   output.o` runs the target assembler and writes an object file. x86 targets
   use `x86_64-elf-as` when available, falling back to GNU `as`; `arm64-macos`
   uses Apple `as -arch arm64`.

9. **Executable link**: `build/rexc input.rx [--target target] -o output`
   assembles a temporary object and links a command-line executable for the
   selected target. On macOS, omitting `--target` selects `arm64-macos` and
   produces a Mach-O executable. Passing `--target i386` or `--target x86_64`
   cross-links ELF output with `x86_64-elf-as` and `x86_64-elf-ld`.

10. **Drunix link**: `build/rexc input.rx --target i386-drunix --drunix-root /path/to/DrunixOS -o
    output.drunix` assembles a temporary i386 object and links it with Drunix's
    startup object, runtime archive, and user linker script. Drunix's current
    checked-in userland runtime is i386-focused; x86_64 output needs an
    x86_64-compatible runtime/startup path before it can be linked this way.

## Core Types

Rexc supports signed integers (`i8`, `i16`, `i32`, `i64`), unsigned integers
(`u8`, `u16`, `u32`, `u64`), `bool`, `char`, `str`, and pointer types written
as `*T`.

The `i386` target emits code for `i8`, `i16`, `i32`, `u8`, `u16`, `u32`,
`bool`, `char`, `str`, and pointer values. The `i64` and `u64` types parse and
type-check, but `i386` code generation fails with a backend diagnostic when a
program needs to load, store, or return those 64-bit values directly.

The `x86_64` target emits code for all current primitive types, including
`i64` and `u64`, and represents pointers as 64-bit addresses using the
Linux/System V x86_64 calling convention.

## Standard Library

Rexc's standard library is being shaped after Rust's layered model, scaled down
to what Rexc can support today. `core` is the always-available,
target-independent contract layer. A future `alloc` layer will own heap-backed
types once Rexc has an allocator contract. `std` is the hosted layer linked
into normal command-line executables.

The current implementation is still a bootstrap stage, but the direction is
Rexc-first and portable by default: hosted `std` functions are declared by
compiler metadata and portable behavior is compiled from Rexc source into the
hosted runtime object. Code should split by target only at the lowest host or
hardware boundary. Target-specific assembly is limited to primitive
ABI/syscall adapters such as `read`, `write`, `exit`, and future allocation
hooks. When stdlib work needs a language feature Rexc does not have yet, the
roadmap is to implement that Rexc capability and then keep moving in Rexc
source.

The source tree mirrors those layers: `src/stdlib/core/` contains
target-independent catalog entries, `src/stdlib/std/` contains hosted prelude
entries, and `src/stdlib/sys/` contains target runtime adapters.

The first `std` milestone exposes a small prelude, so programs can call
standard functions without module syntax:

| Function | Type | Behavior |
| --- | --- | --- |
| `print` | `fn(str) -> i32` | Writes a string to stdout without adding a newline. |
| `println` | `fn(str) -> i32` | Writes a string to stdout followed by `\n`. |
| `read_line` | `fn() -> str` | Reads one stdin line into a Rexc-owned static 1024-byte buffer and returns it as `str`. |
| `strlen` | `fn(str) -> i32` | Returns the byte length of a null-terminated string. |
| `str_is_empty` | `fn(str) -> bool` | Returns whether a null-terminated string has zero bytes before its terminator. |
| `str_eq` | `fn(str, str) -> bool` | Compares two null-terminated byte strings for equality. |
| `str_starts_with` | `fn(str, str) -> bool` | Returns whether the first string starts with the second string. |
| `str_ends_with` | `fn(str, str) -> bool` | Returns whether the first string ends with the second string. |
| `str_contains` | `fn(str, str) -> bool` | Returns whether the first string contains the second string. |
| `str_find` | `fn(str, str) -> i32` | Returns the first byte index of the second string in the first, or `-1` when absent. |
| `memset_u8` | `fn(*u8, u8, i32) -> i32` | Writes one byte value into a raw byte buffer and returns the requested byte count. |
| `memcpy_u8` | `fn(*u8, *u8, i32) -> i32` | Copies bytes between raw byte buffers and returns the requested byte count. |
| `str_copy_to` | `fn(*u8, str, i32) -> i32` | Copies a string into a raw byte buffer, null-terminates when capacity allows, and returns bytes copied. |
| `alloc_bytes` | `fn(i32) -> *u8` | Reserves bytes from the bootstrap bump arena and returns a raw byte pointer. |
| `alloc_str_copy` | `fn(str) -> str` | Copies a string into the bootstrap bump arena and returns the allocated string view. |
| `alloc_str_concat` | `fn(str, str) -> str` | Concatenates two strings into the bootstrap bump arena and returns the allocated string view. |
| `alloc_i32_to_str` | `fn(i32) -> str` | Formats an `i32` into the bootstrap bump arena and returns the allocated string view. |
| `alloc_bool_to_str` | `fn(bool) -> str` | Formats a bool into the bootstrap bump arena as `true` or `false`. |
| `alloc_char_to_str` | `fn(char) -> str` | Formats one byte-sized character into the bootstrap bump arena. |
| `alloc_remaining` | `fn() -> i32` | Returns remaining bytes in the bootstrap bump arena. |
| `alloc_reset` | `fn() -> i32` | Resets the bootstrap bump arena offset to zero. |
| `print_i32` | `fn(i32) -> i32` | Writes a signed decimal integer without adding a newline. |
| `println_i32` | `fn(i32) -> i32` | Writes a signed decimal integer followed by `\n`. |
| `print_bool` | `fn(bool) -> i32` | Writes `true` or `false` without adding a newline. |
| `println_bool` | `fn(bool) -> i32` | Writes `true` or `false` followed by `\n`. |
| `print_char` | `fn(char) -> i32` | Writes one character without adding a newline. |
| `println_char` | `fn(char) -> i32` | Writes one character followed by `\n`. |
| `parse_i32` | `fn(str) -> i32` | Parses a signed decimal integer, returning `0` for invalid or overflow input. |
| `read_i32` | `fn() -> i32` | Reads one input line and parses it as `i32`. |
| `parse_bool` | `fn(str) -> bool` | Parses `true`, returning `false` for any other value until result types exist. |
| `read_bool` | `fn() -> bool` | Reads one input line and parses it as `bool`. |
| `exit` | `fn(i32) -> i32` | Terminates the process with the given status. |
| `panic` | `fn(str) -> i32` | Writes `panic: ` plus the message, then terminates with status `101`. |

`read_line` strips one trailing newline when present, always null-terminates the
buffer, and overwrites the same buffer on the next `read_line` call. It is
implemented in Rexc using a `static mut [u8; 1024]` buffer and the primitive
`sys_read` hook. `strlen`, `str_is_empty`, `str_eq`, `str_starts_with`,
`str_ends_with`, `str_contains`, `str_find`, and `parse_i32` are early `core`-style
target-independent contracts implemented in Rexc source. `memset_u8`,
`memcpy_u8`, and `str_copy_to` provide raw byte-buffer building blocks for
future `alloc` work while Rexc is still growing ownership and heap types.
`alloc_bytes`, `alloc_str_copy`, `alloc_str_concat`, `alloc_i32_to_str`,
`alloc_bool_to_str`, `alloc_char_to_str`, `alloc_remaining`, and `alloc_reset`
are the first bootstrap `alloc` surface: a portable bump arena implemented in
Rexc with a static byte buffer and mutable scalar offset.
`parse_i32` accepts
an optional leading `-` followed by decimal digits; empty strings, invalid
characters, and overflow return `0` until Rexc has richer result types.

Example:

```rust
fn main() -> i32 {
    println("name?");
    let name: str = read_line();
    print("hello ");
    println(name);
    return 0;
}
```

Executable builds link the hosted `std` runtime automatically:

```sh
build/rexc examples/std_io.rx -o build/std_io
printf 'friend\n' | build/std_io
```

Assembly-only (`-S`) and object-only (`-c`) builds can reference standard
library symbols, but they do not include the runtime object. Source-level
prelude names are `print`, `println`, `read_line`, `strlen`, `str_is_empty`,
`str_eq`, `str_starts_with`, `str_ends_with`, `str_contains`, `str_find`,
`print_i32`, `println_i32`, `parse_i32`, `read_i32`, and `exit`. ELF assembly
references those names directly. `panic` is also in the hosted prelude and is
implemented in Rexc on top of `sys_write` and `sys_exit`. `arm64-macos`
assembly references Darwin symbols with leading underscores. The hosted target
adapters provide only the primitive `sys_read`, `sys_write`, and `sys_exit`
hooks needed by the Rexc stdlib source.

### Standard Library Roadmap

Rexc's stdlib should move toward this Rust-style shape:

| Layer | Role | Rexc direction |
| --- | --- | --- |
| `core` | Target-independent primitives and compiler-known contracts. No OS, process, file, terminal, or heap dependency. | Keep primitive operations, string/slice contracts, panic/abort hooks, and eventually traits here. |
| `alloc` | Heap-backed library types that require an allocator but not an OS. | Add after Rexc has allocator hooks; this is where owned strings, vectors, and boxed values should live. |
| `std` | Hosted OS integration layered on `core` and `alloc`. | Keep console I/O, process exit, files, environment, arguments, and platform services here. |
| `sys`/runtime adapters | Narrow target- and OS-specific bridge code. | Split by target triple such as `i386-linux`, `i386-drunix`, `x86_64-linux`, and `arm64-macos`; keep assembly here only when ABI or syscall details require it. |

Critical rule: stdlib behavior should be implemented in portable Rexc. Split
code by target only at the lowest hardware or host boundary: primitive read,
write, exit, allocation, and similar effects. Do not sidestep a missing Rexc
capability by copying the behavior into each architecture file. If `std` needs
byte indexing, static buffers, slices, allocation, result types, or another
language feature, implement that feature in Rexc's compiler/backend first, then
implement the library operation in Rexc.

Canonical stdlib implementation files should be `.rx` files. C++ in
`src/stdlib/` should catalog, load/embed, and compile those files, plus provide
primitive `sys` adapters. Embedded C++ source strings are temporary bootstrap
bridges, not the desired home for portable stdlib code.

The prelude catalog should also become Rexc-source driven. Instead of keeping
parallel C++ signature lists for ordinary stdlib functions, the compiler should
parse the stdlib `.rx` modules first, extract their public function signatures,
and inject or import those declarations into user programs during semantic
analysis and IR lowering. The old per-layer C++ `library.cpp`/`library.hpp`
catalog stubs have been removed; compiler-facing glue now lives in
`src/stdlib/stdlib.cpp`, source embedding, and primitive `sys` adapters.

Near-term stdlib work should continue moving portable implementations out of
target adapters and removing duplicated metadata. String length, comparison,
emptiness, prefix/suffix/search helpers, integer formatting, parsing, line
reading, allocation helpers, and later collection logic should be implemented
once in Rexc library source, with signatures discovered from that source and
only `read`, `write`, `exit`, allocation, and similar host hooks supplied per
target.

## Operators And Control Flow

Rexc supports integer arithmetic with `+`, `-`, `*`, and `/`. Integer
comparisons are supported with `==`, `!=`, `<`, `<=`, `>`, and `>=`; comparison
results have type `bool`. Both comparison operands must be integers with the
same type. Signed integer comparisons use signed condition codes, and unsigned
integer comparisons use unsigned condition codes.

Boolean operators are supported with unary `!` plus short-circuiting `&&` and
`||`. All logical operands must have type `bool`, and the result has type
`bool`.

Explicit casts use `as`. The first cast surface supports integer-to-integer
casts, `bool` to integer casts, and `char as u32`. Casts involving `str`, and
other character casts such as `char as u8`, are rejected.

Function calls can be used as expressions or as statements. Call statements are
intended for side-effecting standard-library functions:

```rust
println("hello");
exit(1);
```

Pointer expressions use `&` to take the address of a mutable local and `*` to
dereference a pointer. Pointer arithmetic supports `pointer + integer` and
`pointer - integer`, with the integer offset scaled by the pointee size.
Indexing is syntax for dereferencing a scaled pointer offset: `p[i]` means
`*(p + i)`. Indirect assignment writes through a pointer:

```rust
fn main() -> i32 {
    let mut x: i32 = 7;
    let p: *i32 = &x;
    *(p + 0) = 9;
    return p[0];
}
```

Rexc also supports `if` and `if/else` statements:

```rust
fn main() -> i32 {
    let left: i32 = 7;
    let right: i32 = 9;
    if left < right {
        return 1;
    } else {
        return 0;
    }
}
```

The `if` condition must be `bool`. Locals declared inside a branch are scoped to
that branch and are not visible after the `if` statement.

Mutable locals use `let mut` and can be updated with assignment statements.
Plain `let` bindings and parameters are immutable.

```rust
fn count() -> i32 {
    let mut value: i32 = 0;
    while value < 10 {
        value = value + 1;
        if value == 3 {
            continue;
        }
        if value == 7 {
            break;
        }
    }
    return value;
}
```

The `while` condition must be `bool`. Locals declared inside the loop body are
scoped to that body and are not visible after the loop. `break` exits the
innermost loop, and `continue` jumps to the next condition check of the
innermost loop. Both are only valid inside loops.

## Build For Drunix Userland

Rexc currently emits assembly for Linux-compatible `i386` and `x86_64`
targets. To build a runnable Drunix userland executable with the current Drunix
runtime, select the `i386` target, assemble a 32-bit object file, then link it
with Drunix's user runtime and linker script.

Set the Drunix checkout path:

```sh
DRUNIX=/Users/drew/development/DrunixOS
```

Build the Rexc compiler:

```sh
cmake -S . -B build
cmake --build build
```

Build the Drunix startup object and runtime archive:

```sh
make -C "$DRUNIX/user" lib/crt0.o lib/libc.a
```

Compile and link the final Drunix ELF executable:

```sh
build/rexc examples/add.rx --target i386-drunix --drunix-root "$DRUNIX" -o build/add.drunix
```

The command above is equivalent to compiling assembly, assembling an i386
object, and linking it with the Drunix user runtime:

```sh
build/rexc examples/add.rx --target i386 -S -o build/add.s
x86_64-elf-as --32 -o build/add.o build/add.s
x86_64-elf-ld -m elf_i386 \
  -T "$DRUNIX/user/user.ld" \
  -o build/add.drunix \
  "$DRUNIX/user/lib/crt0.o" \
  build/add.o \
  "$DRUNIX/user/lib/libc.a"
```

Verify the output:

```sh
file build/add.drunix
x86_64-elf-readelf -h build/add.drunix
```

The result should be a 32-bit Intel 80386 executable ELF. Drunix's `crt0.o`
provides `_start`, prepares `argc`, `argv`, and `envp`, calls `main`, and exits
through the Drunix syscall runtime. Keep `lib/libc.a` after `build/add.o` so
the linker pulls only the archive members needed by the program.

## Build 32-Bit And 64-Bit Objects

To produce a 32-bit Linux-compatible object:

```sh
build/rexc examples/add.rx --target i386 -c -o build/add32.o
```

To produce a 64-bit Linux-compatible object:

```sh
build/rexc examples/wide.rx --target x86_64 -c -o build/wide64.o
```

Validate the object classes:

```sh
x86_64-elf-readelf -h build/add32.o | grep -E 'Class|Machine'
x86_64-elf-readelf -h build/wide64.o | grep -E 'Class|Machine'
```

Expected fields:

```text
Class:                             ELF32
Machine:                           Intel 80386

Class:                             ELF64
Machine:                           Advanced Micro Devices X86-64
```

Linking the 64-bit object into a final executable requires an x86_64 startup
object, runtime library, and linker script that match the Linux-compatible ABI.
The checked-in Drunix user runtime is currently i386-focused, so `x86_64`
support is ready at the compiler/object level and needs matching OS runtime
pieces before it can boot as a Drunix user program.

## Validate An ELF Binary

Use `file` for a quick human-readable check:

```sh
file build/add.drunix
```

Expected output includes:

```text
ELF 32-bit LSB executable, Intel 80386
```

Use `readelf` to inspect the ELF header:

```sh
x86_64-elf-readelf -h build/add.drunix
```

Key fields for a Drunix x86 user program:

```text
Class:                             ELF32
Type:                              EXEC (Executable file)
Machine:                           Intel 80386
Entry point address:               0x400000
```

For a minimal byte-level check, confirm the ELF magic number:

```sh
xxd -l 4 build/add.drunix
```

Expected first four bytes:

```text
7f 45 4c 46
```

## Optional Assembly Check

`ctest` runs an assembler smoke check when `x86_64-elf-as` or GNU `as` is
available. The check compiles arithmetic, branch, and 64-bit examples for the
appropriate targets, then assembles those outputs into ELF object files.
