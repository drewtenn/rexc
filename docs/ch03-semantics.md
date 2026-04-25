\newpage

## Chapter 3 - Semantic Analysis and Types

### When Grammar Is Not Enough

By the time semantic analysis begins, the parser has already done its job. Rexc
has a tree made of functions, statements, expressions, and type names. That
tree is syntactically valid. It is not necessarily meaningful.

Semantic analysis is the compiler stage that checks meaning. It asks whether
names refer to something real, whether types line up, whether calls have the
right number of arguments, and whether literals fit in the type the program
asked for. This is the first stage that can say, "the program is written in the
language's grammar, but it still cannot run."

### The Primitive Type Model

Rexc currently has a small systems-language type set:

| Family | Types |
| --- | --- |
| Signed integers | `i8`, `i16`, `i32`, `i64` |
| Unsigned integers | `u8`, `u16`, `u32`, `u64` |
| Other scalar values | `bool`, `char`, `str` |

A type name in the AST is only source spelling. During semantic analysis, Rexc
parses that spelling into a primitive type value. That value is what the rest
of the compiler compares. Two source locations that both say `i32` become the
same internal type. A misspelled type never gets that far; it becomes a
diagnostic attached to the source location where the bad name appeared.

The distinction between signed and unsigned integers matters beyond range
checking. It affects division and comparisons later in code generation. Signed
values use signed CPU instructions and signed condition codes. Unsigned values
use unsigned ones. Semantic analysis records enough type information for the
backend to choose correctly without reinterpreting source text.

### Names, Locals, and Local Mutation

The analyzer walks each function with a table of visible local names. Function
parameters enter the table first. They are visible in the function body, but
they are not assignable. A `let` statement checks its initializer before
inserting the new name, so a declaration such as `let x: i32 = x;` is still an
unknown-name error rather than a self-reference that silently works.

Rexc separates declaration from mutation. A plain `let` creates an immutable
local. A `let mut` creates a mutable local. Assignment is only valid when the
target name already exists and was declared mutable:

```rust
fn count() -> i32 {
    let mut value: i32 = 0;
    value = value + 1;
    return value;
}
```

The right side of an assignment is checked against the target local's declared
type. Assigning a `bool` into an `i32`, assigning to a plain `let`, assigning
to a parameter, or assigning to an unknown name all produce diagnostics.

### Block Scopes

Branches get their own copies of the local table. When Rexc analyzes an `if`
statement, it checks the condition, then analyzes the `then` body with one
local table and the `else` body with another. A local declared inside a branch
is visible later in that same branch, but it does not leak out to the code
after the `if`. That rule keeps the current language simple and prevents a
later statement from depending on a local that might not have been declared at
runtime.

Loops use the same scoping idea. A `while` condition is checked in the
surrounding local table, and the loop body is analyzed with a copy of that
table. Locals declared inside the loop body are visible later in that body, but
they do not leak out after the loop. Mutations to already-visible mutable
locals are allowed because assignment changes an existing slot rather than
introducing a new name.

Loop-control statements add one more fact for sema to track: whether the
current statement is inside a loop. A `break` exits the innermost loop, and a
`continue` skips to that loop's next condition check. Both are meaningful only
while a loop is being analyzed. If either appears in a function body or an
`if` branch that is not nested inside a loop, Rexc reports a diagnostic.

### Expressions Must Produce the Promised Type

Expected types flow into expressions from their context. A return expression is
checked against the function's return type. A local initializer is checked
against the local's declared type. A function-call argument is checked against
the corresponding parameter type.

This is especially important for integer literals. The literal text `1` does
not name a width on its own. If it initializes a `u8`, Rexc checks it as a
`u8`. If it returns from an `i64` function, Rexc checks it as an `i64`. The
literal carries its decimal spelling from the lexer so this range check can be
done without accidental host-side overflow.

Arithmetic operators require integer operands of the same type and produce that
same type. Comparison operators also require same-typed integer operands, but
they produce `bool`. An `if` or `while` condition must already be `bool`; Rexc
does not silently treat integers as truthy or falsy.

### Where the Compiler Is by the End of Chapter 3

Rexc can now reject programs that are grammatically valid but semantically
wrong. It knows which functions exist, which locals are visible, which primitive
types are valid, which calls match their signatures, and which expressions
produce which types. It also knows which locals are mutable, which assignments
are legal, which branch and loop conditions are boolean, and which `break` and
`continue` statements are actually inside loops.

The compiler has not emitted anything yet. It still holds the source-shaped AST,
now checked for meaning. The next step is to lower that tree into a smaller
typed representation built for the backend rather than for the parser.
