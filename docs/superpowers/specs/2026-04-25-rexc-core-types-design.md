# Rexc Core Primitive Types Design

## Purpose

Rexc currently supports only `i32`. This change adds Rust-like primitive core
types while keeping the compiler implementation in C++17 and preserving the
existing pipeline:

```text
source.rx -> parser -> AST -> semantic analysis -> typed IR -> i386 assembly
```

The goal is to make the language feel like a systems language early, without
forcing ownership, aggregate layout, or a full string runtime into this first
type expansion.

## Goals

- Add signed integer types: `i8`, `i16`, `i32`, `i64`.
- Add unsigned integer types: `u8`, `u16`, `u32`, `u64`.
- Add `bool`, `char`, and `str`.
- Add literals for booleans, characters, and strings.
- Type-check locals, parameters, returns, calls, and arithmetic against the new
  primitive types.
- Emit working i386 assembly for values that fit the current 32-bit backend.
- Produce clear diagnostics for types or operations the parser accepts but the
  backend cannot yet lower.

## Non-Goals

- Pointers, references, arrays, slices, structs, or heap-owning `String`.
- Integer casts, promotion, inference, or mixed-type arithmetic.
- Boolean operators such as `&&`, `||`, and `!`.
- Character or string indexing.
- String concatenation, length queries, or formatting.
- Full 64-bit integer code generation on i386.

## Type Surface

The initial primitive type set is:

```rust
i8 i16 i32 i64
u8 u16 u32 u64
bool
char
str
```

Examples:

```rust
fn main() -> i32 {
    let signed: i32 = -12;
    let unsigned: u32 = 42;
    let enabled: bool = true;
    let letter: char = 'x';
    let message: str = "hello";
    return signed;
}
```

## Literal Semantics

Integer literals are decimal initially. A leading `-` is parsed as unary negation
rather than part of the literal token.

Boolean literals are:

```rust
true
false
```

Character literals use single quotes and lower to integer scalar values:

```rust
'a'
'\n'
'\''
'\\'
```

String literals use double quotes and support the same basic escapes:

```rust
"hello"
"line\n"
"quote: \""
"slash: \\"
```

String literals have type `str`. In this milestone, `str` is represented as the
address of a null-terminated read-only byte sequence in generated assembly.

## Semantic Rules

Types must match exactly. There is no implicit widening or conversion.

Local declarations require initializer type equality:

```rust
let x: u32 = 1;
```

Function call argument types must match parameter types exactly.

Return expressions must match the declared return type exactly.

Arithmetic operators `+`, `-`, `*`, and `/` work only on integer operands with
the same type. The result has that same type.

Unary `-` works only on signed integer types.

`bool`, `char`, and `str` values cannot be used with arithmetic operators.

## IR Model

Replace the single `ir::Type::I32` value with a primitive type model that can
represent:

- kind: signed integer, unsigned integer, bool, char, str.
- bit width for integer types.

AST type names should be resolved into this IR type during semantic analysis or
lowering. The IR should not depend on raw strings such as `"i32"` once a type has
been resolved.

IR values should gain:

- boolean literals.
- character literals.
- string literals.
- unary negation.

The IR should keep string literal payloads so code generation can assign labels
and emit `.rodata`.

## i386 Backend Rules

The current i386 backend will support these value categories:

- `i8`, `i16`, `i32`, `u8`, `u16`, `u32`: use one 32-bit stack slot.
- `bool`: use one 32-bit stack slot with `0` for `false` and `1` for `true`.
- `char`: use one 32-bit stack slot containing the scalar value.
- `str`: use one 32-bit stack slot containing the address of a string literal.

When storing or returning narrower integer values, the backend may keep the
value in a full 32-bit register. Range enforcement is a semantic concern, not a
backend storage-layout concern, in this milestone.

`i64` and `u64` are accepted by the frontend and semantic model, but i386 code
generation rejects any function body that requires lowering a 64-bit value. The
diagnostic should explain that 64-bit integer code generation is not implemented
for the i386 backend yet.

String literals are emitted in a read-only data section:

```asm
.section .rodata
.Lstr0:
    .asciz "hello"
.text
```

A string expression loads the label address:

```asm
movl $.Lstr0, %eax
```

## Diagnostics

Required diagnostics include:

- unknown type name.
- literal does not fit declared integer type.
- unary `-` used on unsigned or non-integer type.
- arithmetic used on non-integer operands.
- arithmetic operands have different types.
- local initializer type mismatch.
- function argument type mismatch.
- return type mismatch.
- 64-bit value used in i386 code generation.

Diagnostics should continue to include source file, line, and column where
source location is available.

## Testing

Tests should cover:

- Parsing all primitive type names.
- Parsing boolean, character, and string literals.
- Semantic acceptance for valid signed, unsigned, bool, char, and str examples.
- Semantic rejection for mismatched primitive types.
- Semantic rejection for arithmetic on bool, char, or str.
- Semantic rejection for unary negation on unsigned types.
- Integer range checks for narrow signed and unsigned declarations.
- Codegen for supported scalar literals and string literals.
- Codegen rejection for `i64` and `u64` use in function bodies.
- CLI smoke compilation for a mixed primitive example.

## Future Direction

Likely follow-up work:

- Proper pointer and reference types.
- `usize` and `isize` once target information is first-class.
- Boolean operators and comparison operators.
- Cast syntax.
- UTF-8 validation policy for source and string literals.
- Proper `str` slice representation with pointer and length.
- Heap-owned `String` once allocation and destructor semantics exist in Rexc.
- Full 64-bit i386 lowering using register pairs or runtime helper calls.
