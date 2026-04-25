# Rexc Compiler Foundation Design

## Purpose

Rexc is a new systems-language compiler intended to target Drunix userland. The
first foundation should make it easy to design the language while proving an
end-to-end path from high-level source to assembly that can be assembled and
linked into an ELF binary with the Drunix toolchain.

The compiler will be written in C++17. The initial target is 32-bit x86 assembly
compatible with the GNU assembler and the existing Drunix i386 ELF user-program
link path.

## Goals

- Provide a working compiler scaffold with clear frontend, semantic, IR, and
  backend boundaries.
- Keep the language grammar easy to change while the language design evolves.
- Emit inspectable i386 assembly that can later be assembled with `as` and
  linked using Drunix's userland runtime and linker script.
- Start with a small systems-language core rather than a large unfinished
  feature set.
- Leave room for future C++- and Rust-like features such as explicit types,
  structs, methods, ownership or lifetime checks, modules, and safer memory
  abstractions.

## Non-Goals For The First Milestone

- Full C++, Rust, or C compatibility.
- Optimization beyond straightforward lowering.
- Borrow checking, lifetime inference, generics, traits, templates, classes, or
  modules.
- A self-hosting compiler.
- Drunix kernel integration.
- Editor tooling beyond keeping the grammar suitable for future reuse.

## Parser Technology

Rexc will use ANTLR 4 for the initial lexer and parser.

ANTLR is a good fit because it provides a modern grammar-driven workflow similar
in spirit to lex/yacc, supports generated C++17 parsers through its C++ runtime,
and keeps syntax experimentation concentrated in `grammar/Rexc.g4`.

The generated parse tree will not be used as the compiler's long-term internal
representation. A hand-written AST builder will convert ANTLR parse trees into
compiler-owned AST nodes. This keeps semantic analysis, IR lowering, and code
generation independent from ANTLR.

## Architecture

The pipeline will be:

```text
source.rx
  -> ANTLR lexer/parser
  -> AST builder
  -> semantic analysis
  -> typed IR
  -> i386 assembly emitter
  -> assembler/linker outside Rexc
```

The main source layout will be:

- `grammar/Rexc.g4`: canonical lexer and parser grammar.
- `src/main.cpp`: command-line driver.
- `src/diagnostics.*`: source locations, compiler errors, and diagnostic
  formatting.
- `src/source.*`: source file loading and line/column mapping.
- `src/ast.*`: compiler-owned abstract syntax tree.
- `src/ast_builder.*`: ANTLR visitor that builds the AST.
- `src/sema.*`: name binding and type checking.
- `src/ir.*`: small typed intermediate representation.
- `src/lower_ir.*`: AST-to-IR lowering.
- `src/codegen_x86.*`: i386 assembly emission.
- `tests/`: focused compiler tests.
- `examples/`: small Rexc programs.

## Initial Language Surface

The first language subset will include:

- `extern fn` declarations.
- `fn` definitions with named parameters and return types.
- `i32` as the first primitive type.
- Integer literals.
- Local bindings with `let name: type = expr;`.
- `return expr;`.
- Function calls.
- Arithmetic expressions: `+`, `-`, `*`, `/`.
- Parenthesized expressions.

Example:

```rust
extern fn puts(s: i32) -> i32;

fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    let value: i32 = add(20, 22);
    return value;
}
```

This syntax is deliberately small but leaves room for systems-language features
without forcing early decisions about ownership, modules, or aggregate types.

## Semantic Model

The first semantic pass will:

- Build a module-level function symbol table.
- Reject duplicate function definitions.
- Reject calls to unknown functions.
- Check function call arity.
- Bind local variables.
- Reject duplicate locals in one function scope.
- Check that local initializers match declared types.
- Check that return expressions match function return types.

Only `i32` values are required initially. Additional primitive types can be added
by extending the type representation and semantic checks.

## Intermediate Representation

The first IR will be simple and typed. It should represent function signatures,
basic expressions, locals, calls, and returns without preserving source syntax.

The IR exists to keep code generation from depending directly on AST shape. This
will make later additions such as control flow, optimization, aggregate layout,
and ownership checks easier to isolate.

## Code Generation

The first backend will emit textual 32-bit x86 assembly in GNU assembler syntax.

Initial conventions:

- Use a simple stack frame with `pushl %ebp`, `movl %esp, %ebp`, and `leave`.
- Pass arguments on the stack using a cdecl-compatible layout.
- Return `i32` values in `%eax`.
- Store local `i32` bindings in stack slots.
- Use straightforward expression code generation rather than optimization.

The compiler will initially support `-S` assembly output. Assembling and linking
can be driven by separate commands or future build integration after the emitted
assembly format is stable.

## Command-Line Interface

Initial CLI:

```sh
rexc input.rx -S -o output.s
```

Expected behavior:

- Read one source file.
- Parse and compile it.
- Write assembly to the path passed with `-o`.
- Print diagnostics to stderr.
- Return nonzero on compile errors.

Future CLI options can add `-c`, full ELF linking, target selection, diagnostic
format control, and integration with Drunix's userland build.

## Build System

The scaffold will use CMake because ANTLR's C++ runtime and generated sources
fit naturally into CMake projects.

The build should:

- Require C++17.
- Locate or configure ANTLR generation.
- Build generated ANTLR C++ sources.
- Build the hand-written compiler sources.
- Build tests.

Dependency handling can start simple and documented. Vendoring ANTLR runtime or
adding a dependency manager can be decided after the first build proves out on
the development machine.

## Testing

Testing will focus on compiler behavior rather than implementation details.

Initial tests should cover:

- Lexer/parser acceptance for minimal valid programs.
- Parser rejection for malformed syntax.
- Semantic rejection for duplicate functions, unknown calls, wrong arity, and
  return type mismatches.
- Golden assembly checks for small functions.
- CLI smoke tests that compile examples to assembly.

The tests should not require booting Drunix. Later integration tests can assemble
and link programs into Drunix ELF binaries once the assembly output is stable.

## Error Handling

All compiler stages will report diagnostics with source file, line, and column
when source context is available. Parser errors from ANTLR should be translated
into Rexc diagnostics rather than printed as raw generated-parser output.

Compilation should stop after parse errors. Semantic analysis may collect
multiple errors where that is straightforward, but correctness is more important
than exhaustive recovery in the first milestone.

## Future Direction

Likely next features after the first end-to-end compiler:

- `bool`, pointer, and `void` types.
- `if`, `while`, and block expressions.
- Structs and field access.
- Explicit address-of and dereference operators.
- A safer ownership or move model inspired by Rust.
- Modules and imports.
- Drunix userland build integration.
- Direct assemble/link commands.
- Additional backends for AArch64 Drunix bring-up.
