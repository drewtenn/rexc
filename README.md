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

## Optional Assembly Check

`ctest` runs an assembler smoke check when `x86_64-elf-as` or GNU `as` is
available. The check compiles `examples/add.rx` to assembly and assembles it as
a 32-bit x86 object file.
