\newpage

## Chapter 8 - Tuples and Slices

### Tuples Group Values Without Naming Them

A **tuple** is a fixed-length record whose components have types but no
names. Tuples are useful when grouping values is worth the small bit of
structure, but inventing field names would not pull its weight.

The type of a tuple is written with parentheses around a comma-separated
list of types:

```rust
let pair: (i32, i32) = (3, 4);
let triple: (i32, bool, char) = (1, true, 'x');
```

A tuple expression has the same shape: parentheses around a list of values.
The compiler checks that each component matches the declared type at its
position.

Tuples are accessed by integer index using the same `.` notation we use for
struct fields:

```rust
let first: i32 = pair.0;
let second: i32 = pair.1;
```

The numbers `0` and `1` are field names, not array indices. They refer to
the first and second components of the tuple. There is no runtime bounds
check involved, because the indices are part of the type.

A tuple type with two or more components is legal. There is no special form
for a "one-tuple". When a function would naturally return one value, we
return that value directly rather than wrapping it.

### When To Reach For a Tuple

Tuples are tempting and easy to overuse. The honest test is whether the
components have a relationship that a future reader will recognise without
help. A pair of `(x, y)` coordinates, a pair of `(low, high)` bounds, a
pair of `(value, count)` for a parsing helper — these all read clearly as
tuples. A pair of "two unrelated configuration knobs" probably wants a
struct with named fields.

When in doubt, prefer a struct. The cost of naming the fields is one line
of declaration. The cost of leaving them anonymous is paid by every reader
who has to remember which one was which.

### Slices Describe a Run of Values

A **slice** is Rexy's way of describing a contiguous run of values of the
same type. Where a tuple says "exactly these components, each with its own
type", a slice says "some number of values, all of this one type". The
slice itself does not own the underlying memory; it is a description of a
run that lives somewhere else.

The slice type is written with `&` and square brackets around the element
type:

```rust
fn sum(values: &[i32]) -> i32 {
    let mut total: i32 = 0;
    // ... iterate and accumulate ...
    return total;
}
```

A slice carries two facts about its underlying run: where it starts and
how long it is. That makes a slice a natural way to write a function that
processes a sequence without committing to where the sequence comes from.
The same `sum` function above could be called with a slice that points at a
static buffer, a slice that points into a heap region, or a slice that
points at part of a larger run.

Slices work hand in hand with the pointer machinery we will introduce in
Chapter 9. The pointer side gives us the address. The slice side gives us
the length, and a uniform shape that functions can name in their
signatures.

### Where We Are by the End of Chapter 8

We can build records (structs), choices (enums), positional groups
(tuples), and contiguous runs (slices). We have the shapes a working Rexy
program needs to model the data it operates on.

What we have not yet seen is how to point *into* memory directly: how to
take the address of a value, how to walk through a buffer, and how to
mutate a static. Part IV introduces pointers, statics, and the `unsafe`
boundary that surrounds them.
