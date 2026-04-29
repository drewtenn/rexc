\newpage

## Chapter 7 - Enums and Variants

### A List Of Alternatives

A struct says "all of these fields, together". An **enum** says
"exactly one of these alternatives". Try a simple enum with no
payloads:

```rust
enum Direction {
    North,
    South,
    East,
    West,
}

fn code(d: Direction) -> i32 {
    match d {
        Direction::North => { return 1; },
        Direction::South => { return 2; },
        Direction::East => { return 3; },
        Direction::West => { return 4; },
    }
}

fn main() -> i32 {
    return code(Direction::East);
}
```

```text
3
```

Each name in the body of the enum is a **variant**. A value of type
`Direction` is exactly one variant, never zero, never more than one.

To build a value, name the enum and the variant with `::`:
`Direction::North`. To read it, you `match`. The compiler tracks which
variant a value holds and uses that tracking to check `match`
statements: if you forget a variant in a `match`, the compiler tells
you about it.

### Variants With Payloads

Variants may carry data. A variant followed by a parenthesized type
list becomes a tagged record:

```rust
enum Outcome {
    Ok(i32),
    Err(i32),
}

fn unwrap_or(o: Outcome, fallback: i32) -> i32 {
    match o {
        Outcome::Ok(value) => { return value; },
        Outcome::Err(_) => { return fallback; },
    }
}

fn main() -> i32 {
    let success: Outcome = Outcome::Ok(42);
    let failure: Outcome = Outcome::Err(1);
    return unwrap_or(success, 0) - unwrap_or(failure, 0);
}
```

```text
42
```

In the first arm, `value` is a fresh local that holds the `i32` carried
by the `Ok` variant. It is in scope for the body of that arm only. In
the second arm, the wildcard `_` says you do not need the payload, only
the variant.

A value of type `Outcome` is either `Outcome::Ok(value)` for some `i32`
value or `Outcome::Err(code)` for some `i32` code. Different variants
of the same enum can carry different types and different numbers of
fields.

### Forgetting A Variant Is A Compile Error

This is one of the places `match` earns its keep. Take the `Outcome`
example and remove an arm:

```rust
fn unwrap_or(o: Outcome, fallback: i32) -> i32 {
    match o {
        Outcome::Ok(value) => { return value; },
    }
}
```

The compiler refuses, telling you the `Err` variant is unhandled. Add
the missing arm back, and the program compiles. If `Outcome` ever
grows a third variant, every `match` in the program that does not
handle the new variant will become a compile error at the point where
the `match` was written. That is the property `if`/`else if` chains
cannot give you.

### Public Enums

Like structs, an enum declared in a module is private by default. To
make the type and its variants visible to other modules, you mark the
enum `pub`:

```rust
pub enum Outcome {
    Ok(i32),
    Err(i32),
}
```

The visibility applies to the enum and all of its variants together.
There is no way to expose one variant without exposing the others, and
that is deliberate: variants only make sense as a set.

### Where You Are by the End of Chapter 7

You can model choice as a type. You can attach data to a variant and
pull that data back out with a `match`. Combined with structs, you now
have the two shapes that almost every program eventually wants: "all
of these, together" and "exactly one of these".

You know:

- `enum Name { Variant, Variant(Type), ... }` declares a tagged-union
  type.
- `Name::Variant(...)` builds a value of that variant.
- `match` over an enum binds the payload of each variant in its arm.
- The compiler enforces that a `match` covers every variant.
- `pub enum` exposes both the type and its variants across module
  boundaries.

There are two more composites worth meeting before you leave the data
chapters. Tuples give you "a fixed group of typed values without naming
them", and slices give you "a contiguous run of values of the same
type". Chapter 8 introduces both.
