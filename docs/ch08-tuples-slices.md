\newpage

## Chapter 8 - Tuples and Slices

### Tuples

A **tuple** is a fixed-length record whose components have types but
no names. Tuples are useful when grouping values is worth the small bit
of structure but inventing field names would not pull its weight.

The type of a tuple is written with parentheses around a
comma-separated list of types:

```rust
fn main() -> i32 {
    let pair: (i32, i32) = (30, 12);
    return pair.0 + pair.1;
}
```

```text
42
```

A tuple expression has the same shape: parentheses around a list of
values. The compiler checks that each component matches the declared
type at its position.

Tuples are accessed by integer index using the same `.` notation you
use for struct fields:

```rust
fn main() -> i32 {
    let triple: (i32, bool, char) = (10, true, 'A');
    return triple.0 + triple.1 as i32 + triple.2 as u32 as i32;
}
```

```text
76
```

`triple.0` is `10`. `triple.1` is `true`, cast to `1`. `triple.2` is
`'A'`, whose code point is `65`. `10 + 1 + 65` is `76`. The numbers
`0`, `1`, and `2` are field names, not array indices. They refer to
the components of the tuple by position. There is no runtime bounds
check involved, because the indices are part of the type.

### Returning A Tuple

A function can return a tuple just like any other type:

```rust
fn split(value: i32) -> (i32, i32) {
    return (value / 10, value % 10);
}

fn main() -> i32 {
    let parts: (i32, i32) = split(42);
    return parts.0 * 10 + parts.1;
}
```

```text
42
```

`split(42)` returns the tuple `(4, 2)`. `main` reads `parts.0` and
`parts.1` and reassembles the original number.

### When To Reach For A Tuple

Tuples are tempting and easy to overuse. The honest test is whether
the components have a relationship that a future reader will recognise
without help. A pair of `(x, y)` coordinates, a pair of `(low, high)`
bounds, a pair of `(value, count)` for a parsing helper — these all
read clearly as tuples. A pair of "two unrelated configuration knobs"
probably wants a struct with named fields.

When in doubt, prefer a struct. The cost of naming the fields is one
line of declaration. The cost of leaving them anonymous is paid by
every reader who has to remember which one was which.

### Slices

A **slice** is Rexy's way of describing a contiguous run of values of
the same type. Where a tuple says "exactly these components, each with
its own type", a slice says "some number of values, all of this one
type".

The slice type is written with `&` and square brackets around the
element type:

```rust
fn first_or(values: &[i32], fallback: i32) -> i32 {
    if len(values) > 0 {
        return values[0];
    }
    return fallback;
}
```

`len(slice)` is a stdlib helper from Chapter 13's prelude that returns
the element count. `slice[index]` reads the element at the given
position. The slice itself does not own the underlying memory; it is a
description of a run that lives somewhere else.

A slice carries two facts about its underlying run: where it starts
and how long it is. That makes a slice a natural way to write a
function that processes a sequence without committing to where the
sequence comes from.

Slices work hand in hand with the pointer machinery you will meet in
Chapter 9. The pointer side gives you the address. The slice side
gives you the length, and a uniform shape that functions can name in
their signatures.

### Where You Are by the End of Chapter 8

You can build records (structs), choices (enums), positional groups
(tuples), and contiguous runs (slices). You have the shapes a working
Rexy program needs to model the data it operates on.

You know:

- `(T, U)` is a tuple type; `(a, b)` is a tuple expression;
  `tuple.0`, `tuple.1`, ... read its components.
- A slice type is `&[T]`; functions take and return slices the way they
  take and return any other type.
- `slice[i]` reads an element; `len(slice)` returns the element count.

What you do not yet have is the ability to point *into* memory
directly: how to take the address of a value, how to walk through a
buffer, and how to mutate a static. Part IV introduces pointers,
statics, and the `unsafe` boundary that surrounds them.
