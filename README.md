# Rexc

Rexc is an experimental systems-language compiler for Drunix userland.

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
build/rexc examples/branch.rx --target i386 -S -o build/branch32.s
build/rexc examples/wide.rx --target x86_64 -S -o build/wide64.s
build/rexc examples/wide.rx --target x86_64 -c -o build/wide64.o
build/rexc examples/add.rx --target arm64-macos -S -o build/add-arm64.s
build/rexc examples/add.rx --target arm64-macos -c -o build/add-arm64.o
build/rexc examples/add.rx --target arm64-macos -o build/add-arm64
```

The default target is `i386`. Use `--target x86_64` to emit 64-bit
Linux-compatible x86_64 assembly or object files. Use `--target arm64-macos`
on Apple Silicon macOS to emit Darwin ARM64 assembly or Mach-O object files.
`-S` writes assembly, while `-c` runs the target assembler and writes an object
file.

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
build/rexc examples/add.rx --drunix-root /path/to/DrunixOS -o build/add.drunix
```

Drunix linking currently targets the i386 runtime path.

## Targets

Rexc currently supports Linux-compatible x86 targets and a Darwin ARM64 target:

| Target | CLI option | Assembler mode | Object class | Notes |
| --- | --- | --- | --- | --- |
| `i386` | omitted or `--target i386` | `--32` | `ELF32` | Default target; matches the current Drunix user runtime. |
| `x86_64` | `--target x86_64` | `--64` | `ELF64` | Uses the Linux/System V x86_64 calling convention. |
| `arm64-macos` | `--target arm64-macos` | Apple `as -arch arm64` | Mach-O 64-bit arm64 object | Uses Darwin symbol names and Apple ARM64 calling convention. |

Both targets emit assembly for functions named in the Rexc source. Final
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

1. **CLI input**: `src/main.cpp` reads the input file, creates a `SourceFile`,
   and owns the top-level flow. It prints diagnostics and exits non-zero if any
   frontend or backend stage fails.

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

7. **Assembly output**: `build/rexc input.rx [--target i386|x86_64] -S -o
   output.s` writes assembly only after code generation succeeds. Failed code
   generation reports diagnostics and does not write partial assembly.

8. **Object assembly**: `build/rexc input.rx [--target i386|x86_64] -c -o
   output.o` runs `x86_64-elf-as` when available, falling back to GNU `as`,
   and writes an ELF object file. Manual assembly still works with
   `x86_64-elf-as --32` or GNU `as --32` for i386 output and
   `x86_64-elf-as --64` or GNU `as --64` for x86_64 output.

9. **ELF link**: `build/rexc input.rx --drunix-root /path/to/DrunixOS -o
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
runtime, assemble the default `i386` output into a 32-bit object file, then link
it with Drunix's user runtime and linker script.

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
build/rexc examples/add.rx --drunix-root "$DRUNIX" -o build/add.drunix
```

The command above is equivalent to compiling assembly, assembling an i386
object, and linking it with the Drunix user runtime:

```sh
build/rexc examples/add.rx -S -o build/add.s
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
