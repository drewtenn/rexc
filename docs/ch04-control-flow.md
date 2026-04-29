\newpage

## Chapter 4 - Conditions and Loops

### `if` and `else`

The simplest form of choice is `if`:

```rust
if value > 0 {
    return 1;
}
```

The condition between `if` and the opening brace must be a `bool`. Rexy will
not silently treat an integer as a condition the way C would. If we want to
test whether `value` is non-zero, we write `value != 0` and let the
comparison produce a `bool`.

`if` accepts an optional `else`, and the `else` may be either another block
or another `if`:

```rust
if value > 0 {
    return 1;
} else if value < 0 {
    return -1;
} else {
    return 0;
}
```

There is no separate ternary operator. The chain of `if/else if/else`
covers every case.

### Boolean Operators

The comparison and boolean operators are the ones a C-family programmer
expects: `==`, `!=`, `<`, `<=`, `>`, `>=` for comparisons, and `&&`, `||`,
`!` for logical combination. `&&` and `||` short-circuit: the right operand
is only evaluated if the left operand did not already determine the result.

```rust
if !right && (left || right) {
    return 1;
}
```

The full precedence is the conventional one. Comparisons bind tighter than
`&&`, which binds tighter than `||`. Parentheses always work and are
encouraged whenever the precedence is not immediately obvious.

### `while` Loops

`while` repeats a block as long as a `bool` condition holds:

```rust
let mut value: i32 = 0;
while value < 10 {
    value = value + 1;
}
```

The condition is checked before each iteration, including the first. If the
condition is `false` to start with, the loop body never runs.

### `for` Loops

Rexy's `for` loop is the C-style three-part form: an initializer, a
condition, and an increment.

```rust
for let mut i: i32 = 0; i < 8; i = i + 1 {
    // body
}
```

The initializer can be a `let` statement or an assignment to an existing
binding. The condition is a `bool` expression. The increment is an
assignment, an indirect assignment through a pointer, or one of the `++`
and `--` increment forms. The parts may also be wrapped in parentheses if
that reads better:

```rust
for (let mut i: i32 = 0; i < 8; ++i) {
    // body
}
```

Use whichever form fits the surrounding code. They produce the same loop.

### `break` and `continue`

Inside a `while` or `for`, `break` exits the loop immediately and `continue`
skips the rest of the current iteration:

```rust
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
```

When the loop above exits, `value` holds `7`. We saw `value` reach `3`,
skipped the rest of that iteration, kept counting, and then `break` ended
the loop the moment `value` hit `7`.

`break` and `continue` only make sense inside a loop. Using them outside
one is a diagnostic, not a runtime error.

### Conditions Stay Boolean

It is worth saying once more, because it is the most common surprise for a
programmer arriving from C: every condition in Rexy must already be a
`bool`. There is no implicit conversion from integer to boolean. If we want
"non-zero", we write `!= 0`. If we want "non-empty string", we write a call
to a string predicate. The language always asks: what does this condition
*mean*?

### Where We Are by the End of Chapter 4

We can branch on conditions, loop while a condition holds, count through a
range with a `for`, and use `break` and `continue` to shape the way a loop
ends or skips iterations. Combined with bindings and functions, that is
enough machinery to write almost any algorithm we have in mind, as long as
the data we are working over is simple values.

When the data has shape, plain `if` chains become awkward. Chapter 5
introduces `match`, the construct Rexy gives us for working with structured
data once we have it.
