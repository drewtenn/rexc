\newpage

## Chapter 7 - Enums and Variants

### Choice as a Type

A **struct** is a record of fields, all present at once. An **enum** is the
opposite shape: exactly one of several alternatives. Enums let us write
types like "either a success with an integer or an error with a code" and
have the compiler enforce that we handle both cases.

The simplest enum is a list of named alternatives:

```rust
enum Direction {
    North,
    South,
    East,
    West,
}
```

Each name in the body is a **variant**. A value of type `Direction` is
exactly one variant, never zero, never more than one. The compiler tracks
which variant a value holds and uses that tracking to check `match`
statements.

### Variants With Payloads

Variants may carry data of their own. A variant followed by a parenthesized
type list becomes a tagged record:

```rust
enum Outcome {
    Ok(i32),
    Err(i32),
}
```

A value of type `Outcome` is either `Ok(value)` for some `i32` value or
`Err(code)` for some `i32` code. Different variants of the same enum can
carry different types and different numbers of fields. Variants without a
payload, like `North` above, are simply variants whose payload list is
empty.

### Building an Enum Value

To build a value of an enum type, we name the enum and the variant with
`::` and supply the payload, if any:

```rust
let win: Outcome = Outcome::Ok(42);
let lose: Outcome = Outcome::Err(1);
let way: Direction = Direction::North;
```

The path `Outcome::Ok` is a qualified name. We will see `::` again in
modules, where the same operator separates module names. For enum
construction, the rule is simply: enum name, then `::`, then variant name.

### Matching on Variants

Enums become useful the moment we `match` on them. The pattern syntax we
saw in Chapter 5 grows two new forms here. A constructor pattern matches a
specific variant and binds its payload:

```rust
match result {
    Outcome::Ok(value) => {
        return value;
    },
    Outcome::Err(_) => {
        return -1;
    },
}
```

In the first arm, `value` is a fresh local that holds the `i32` carried by
the `Ok` variant. It is in scope for the body of that arm only. In the
second arm, the wildcard `_` says we do not need the payload, only the
variant.

For variants whose payload looks like a struct, a struct-shaped binding list
inside braces lets us name each field individually. Most of the enums we
write will use the parenthesized payload form, but the brace form exists for
the cases where naming each field reads better than positional binding.

### Why `match` Earns Its Keep Here

When the value being matched is an enum, Rexy can tell us about variants we
forgot. If `Outcome` grows a third variant later, every `match` over an
`Outcome` that does not handle the new variant becomes a diagnostic at the
point where the `match` was written. That is the property `if`/`else if`
chains cannot give us: enums plus `match` make new alternatives a checked
change, not a silent one.

### Public Enums

Like structs, an enum declared in a module is private by default. To make
the type and its variants visible to other modules, we mark the enum `pub`:

```rust
pub enum Outcome {
    Ok(i32),
    Err(i32),
}
```

The visibility applies to the enum and all of its variants together. There
is no way to expose one variant without exposing the others, and that is
deliberate: variants only make sense as a set.

### Where We Are by the End of Chapter 7

We can model choice as a type. We can attach data to a variant and pull
that data back out with a `match`. Combined with structs, we now have the
two shapes that almost every program eventually wants: "all of these,
together" and "exactly one of these".

There are two more composites worth meeting before we leave the data
chapters. Tuples give us "a fixed group of typed values without naming
them", and slices give us "a contiguous run of values of the same type".
Chapter 8 introduces both.
