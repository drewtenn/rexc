# Rexc

Rexc is an experimental systems-language compiler for Drunix userland.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Compile To Assembly

```sh
build/rexc examples/add.rx -S -o build/add.s
```

The generated assembly is GNU assembler-compatible 32-bit x86 text intended for
the Drunix userland toolchain.

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
  -> i386 assembly
  -> assembler object
  -> Drunix ELF link
```

1. **CLI input**: `src/main.cpp` reads the input file, creates a `SourceFile`,
   and owns the top-level flow. It prints diagnostics and exits non-zero if any
   frontend or backend stage fails.

2. **Lexing and parsing**: `src/parse.cpp` tokenizes the source and parses
   functions, extern declarations, statements, expressions, primitive type
   names, and literals. The parser builds the AST types declared in
   `include/rexc/ast.hpp`.

3. **AST**: The AST preserves source-level structure: functions, parameters,
   `let` statements, `return` statements, names, calls, unary/binary
   expressions, and integer/bool/char/string literals. Integer literals keep
   their original decimal text so later stages can range-check large values
   without parser overflow.

4. **Semantic analysis**: `src/sema.cpp` validates names, duplicate functions
   and locals, function calls, return types, initializer types, arithmetic
   operands, unary operators, and integer literal ranges. It uses the primitive
   type helpers in `src/types.cpp` so all frontend checks share one type model.

5. **IR lowering**: `src/lower_ir.cpp` converts the checked AST into the typed
   IR in `include/rexc/ir.hpp`. The IR carries resolved primitive types on
   functions, parameters, locals, calls, literals, unary expressions, and binary
   expressions. This gives the backend a smaller, typed representation to emit.

6. **i386 code generation**: `src/codegen_x86.cpp` emits GNU assembler syntax
   for 32-bit x86. It emits supported scalar values in 32-bit stack slots,
   emits strings in `.rodata` with `.LstrN` labels, uses signed or unsigned
   division based on IR type, and reports backend diagnostics for unsupported
   `i64` and `u64` code generation.

7. **Assembly output**: `build/rexc input.rx -S -o output.s` writes assembly
   only after code generation succeeds. Failed code generation reports
   diagnostics and does not write partial assembly.

8. **Object assembly**: Use `x86_64-elf-as --32` or GNU `as --32` to assemble
   the generated `.s` file into an i386 object file.

9. **Drunix ELF link**: Link the object with Drunix's `crt0.o`, `libc.a`, and
   user linker script to produce the final 32-bit ELF executable.

## Core Primitive Types

Rexc supports signed integers (`i8`, `i16`, `i32`, `i64`), unsigned integers
(`u8`, `u16`, `u32`, `u64`), `bool`, `char`, and `str`.

The i386 backend emits code for `i8`, `i16`, `i32`, `u8`, `u16`, `u32`,
`bool`, `char`, and `str`. The `i64` and `u64` types parse and type-check, but
code generation fails with a backend diagnostic because 64-bit integer emission
is not implemented for i386 yet.

## Build For Drunix Userland

Rexc currently emits assembly. To build a runnable Drunix userland executable,
assemble that output into a 32-bit i386 object file, then link it with Drunix's
user runtime and linker script.

Set the Drunix checkout path:

```sh
DRUNIX=/Users/drew/development/DrunixOS
```

Build the Rexc compiler and compile the example program:

```sh
cmake -S . -B build
cmake --build build

build/rexc examples/add.rx -S -o build/add.s
x86_64-elf-as --32 -o build/add.o build/add.s
```

Build the Drunix startup object and runtime archive:

```sh
make -C "$DRUNIX/user" lib/crt0.o lib/libc.a
```

Link the final Drunix ELF executable:

```sh
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
available. The check compiles `examples/add.rx` to assembly and assembles it as
a 32-bit x86 object file.
