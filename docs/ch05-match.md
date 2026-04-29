\newpage

## Chapter 5 - Pattern Matching with `match`

### When `if` Chains Stop Helping

Suppose a function takes an integer command code and dispatches on it:

```rust
fn handle(code: i32) -> i32 {
    if code == 0 {
        return 100;
    } else if code == 1 || code == 2 {
        return 101;
    } else {
        return -1;
    }
}
```

That works, but the structure is hidden in repetition. Each arm restates
`code ==`. The fallback case sits at the end as an `else`. The relationship
between the cases is not visible at a glance.

Rexy provides a structured form for this kind of dispatch: `match`.

### The Shape of a `match`

A `match` chooses one block to execute based on the shape of a value:

```rust
match code {
    0 => { return 100; },
    1 | 2 => { return 101; },
    _ => { return -1; },
}
```

The expression after `match` is the value we are testing. Inside the braces
is a list of **arms**. Each arm has the shape `pattern => block`. The first
arm whose pattern matches the value is the arm that runs.

Arms are separated by commas, and a trailing comma after the last arm is
allowed. Each arm's body is a block, even when that block is a single
statement. The block contains the same kind of statements we use anywhere
else.

### Patterns

The simplest patterns are literals: integers, booleans, characters. They
match a value if the value is equal to the literal:

```rust
match c {
    'a' | 'e' | 'i' | 'o' | 'u' => { return 1; },
    _ => { return 0; },
}
```

The `|` between two patterns is an alternation. An alternation arm matches
if any of the listed patterns match. The `_` pattern is a wildcard. It
matches any value and is how we express "everything else".

Negative integer literals work too, so we can match a signed integer
directly:

```rust
match value {
    -1 => { return 0; },
    0 => { return 1; },
    1 => { return 2; },
    _ => { return 3; },
}
```

Once we introduce structs and enums in Part III, patterns will grow to
include constructor patterns that destructure variants and struct patterns
that bind fields by name. We will revisit `match` then with those patterns
in hand. For now, literals and the wildcard cover everything we need.

### `match` is a Statement

`match` is a statement, not an expression. It does not produce a value the
way a `match` does in some other languages. To get a value out of a `match`,
each arm `return`s, or each arm assigns to a `mut` binding declared before
the `match`:

```rust
let mut label: i32 = 0;
match value {
    0 => { label = 100; },
    1 | 2 => { label = 101; },
    _ => { label = -1; },
}
```

Both styles are reasonable Rexy. The `return`-from-each-arm style suits a
function whose only job is to dispatch. The "assign into a `mut` binding"
style suits a function that needs the value for further work.

### Why Coverage Matters

A `match` statement should account for every value the matched type can
hold. For integers, the wildcard covers the cases we did not list. For
booleans, the only legal values are `true` and `false`. Once we have enums,
the compiler can tell us when we forgot a variant, and that will be one of
the places `match` earns its keep most clearly.

### Where We Are by the End of Chapter 5

We have two complementary tools for control flow. `if` chains handle
conditions where the condition is a boolean computation. `match` handles
choice over the value or shape of a data piece. Together they cover the
cases the programs we will write actually need.

What we do not yet have is the data those programs will operate on. Part
III introduces structs, enums, tuples, and slices. Once we have composite
types, every construct from Part II becomes much more useful.
