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

Build the Drunix user runtime objects:

```sh
make -C "$DRUNIX/user" \
  lib/crt0.o lib/cxx_init.o lib/syscall.o lib/malloc.o \
  lib/string.o lib/ctype.o lib/stdlib.o lib/stdio.o \
  lib/unistd.o lib/time.o
```

Link the final Drunix ELF executable:

```sh
x86_64-elf-ld -m elf_i386 \
  -T "$DRUNIX/user/user.ld" \
  -o build/add.drunix \
  "$DRUNIX/user/lib/crt0.o" \
  "$DRUNIX/user/lib/cxx_init.o" \
  "$DRUNIX/user/lib/syscall.o" \
  "$DRUNIX/user/lib/malloc.o" \
  "$DRUNIX/user/lib/string.o" \
  "$DRUNIX/user/lib/ctype.o" \
  "$DRUNIX/user/lib/stdlib.o" \
  "$DRUNIX/user/lib/stdio.o" \
  "$DRUNIX/user/lib/unistd.o" \
  "$DRUNIX/user/lib/time.o" \
  build/add.o
```

Verify the output:

```sh
file build/add.drunix
x86_64-elf-readelf -h build/add.drunix
```

The result should be a 32-bit Intel 80386 executable ELF. Drunix's `crt0.o`
provides `_start`, prepares `argc`, `argv`, and `envp`, calls `main`, and exits
through the Drunix syscall runtime.

## Optional Assembly Check

`ctest` runs an assembler smoke check when `x86_64-elf-as` or GNU `as` is
available. The check compiles `examples/add.rx` to assembly and assembles it as
a 32-bit x86 object file.
