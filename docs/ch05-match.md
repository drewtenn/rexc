\newpage

## Chapter 5 - Pattern Matching with `match`

### When `if` Chains Stop Helping

Suppose you want to dispatch on a small set of integer codes:

```rust
fn handle(code: i32) -> i32 {
    if code == 0 {
        return 100;
    } else if code == 1 {
        return 101;
    } else if code == 2 {
        return 101;
    } else {
        return -1;
    }
}

fn main() -> i32 {
    return handle(2);
}
```

```text
101
```

That works, but the structure is hidden in repetition. Each arm
restates `code ==`. The fallback case sits at the end as an `else`. The
relationship between the cases is not visible at a glance.

Rexy gives you a structured form for this kind of dispatch: `match`.

### The Shape Of A `match`

Rewrite `handle` with `match`:

```rust
fn handle(code: i32) -> i32 {
    match code {
        0 => { return 100; },
        1 | 2 => { return 101; },
        _ => { return -1; },
    }
}

fn main() -> i32 {
    return handle(2);
}
```

```text
101
```

The expression after `match` is the value you are testing. Inside the
braces is a list of **arms**. Each arm has the shape `pattern => block`.
The first arm whose pattern matches the value is the arm that runs.

Arms are separated by commas, and a trailing comma after the last arm
is allowed. Each arm's body is a block, even when that block is a
single statement.

### Patterns

The simplest patterns are literals: integers, booleans, characters.
They match a value if the value is equal to the literal:

```rust
fn vowel_count(c: char) -> i32 {
    match c {
        'a' | 'e' | 'i' | 'o' | 'u' => { return 1; },
        _ => { return 0; },
    }
}

fn main() -> i32 {
    return vowel_count('e') + vowel_count('z') + vowel_count('o');
}
```

```text
2
```

The `|` between two patterns is **alternation**. An alternation arm
matches if any of the listed patterns match. The `_` pattern is a
**wildcard**: it matches any value and is how you express "everything
else".

Negative integer literals work too:

```rust
fn sign(value: i32) -> i32 {
    match value {
        -1 => { return 0; },
        0 => { return 1; },
        1 => { return 2; },
        _ => { return 3; },
    }
}

fn main() -> i32 {
    return sign(0);
}
```

```text
1
```

Once you meet structs and enums in Part III, patterns will grow to
include constructor patterns that destructure variants and struct
patterns that bind fields by name. You will revisit `match` then with
those patterns in hand. For now, literals and the wildcard cover
everything you need.

### `match` Is A Statement

`match` is a statement, not an expression. It does not produce a value
the way it does in some other languages. To get a value out of a
`match`, each arm `return`s, or each arm assigns to a `mut` binding
declared before the `match`:

```rust
fn label(value: i32) -> i32 {
    let mut result: i32 = 0;
    match value {
        0 => { result = 100; },
        1 | 2 => { result = 101; },
        _ => { result = -1; },
    }
    return result;
}

fn main() -> i32 {
    return label(0);
}
```

```text
100
```

Both styles are reasonable Rexy. The `return`-from-each-arm style suits
a function whose only job is to dispatch. The "assign into a `mut`
binding" style suits a function that needs the value for further work.

### Coverage Matters

A `match` should account for every value the matched type can hold.
For integers, the wildcard covers the cases you did not list. For
booleans, the only legal values are `true` and `false`. Once you have
enums, the compiler will tell you when you forgot a variant, which is
one of the places `match` earns its keep most clearly.

### Where You Are by the End of Chapter 5

You can dispatch on a value with `match`, write literal and wildcard
patterns, and combine alternatives with `|`.

You know:

- `match value { pattern => { block }, ... }` chooses the first
  matching arm.
- Patterns can be integer literals, boolean literals, character
  literals, alternations with `|`, or `_` for "everything else".
- `match` is a statement; arms either `return` or assign into a `mut`
  binding declared before the `match`.

What you do not yet have is the data those programs will operate on.
Part III introduces structs, enums, tuples, and slices. Once you have
composite types, every construct from Parts I and II becomes much more
useful.
