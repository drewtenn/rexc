\newpage

## Chapter 2 - Values, Types, and Casts

### Why You Have To Write Types Down

Rexy is a typed language with no inference at the binding level. Every
local you declare, every function parameter, every return type, and
every static carries a written-down type. That is a deliberate cost. It
buys two things that matter for a systems language: a reader of your
program never has to guess what a name holds, and the compiler never
has to guess either.

You will start meeting the Rexy type system in Chapter 3 when you
introduce locals. For now, this chapter is about the *values* the type
system describes. You will see each one running inside `main`, the only
function you know how to write so far.

### Integers

Rexy gives you eight integer types in two families:

| Family | Types | Notes |
| --- | --- | --- |
| Signed | `i8`, `i16`, `i32`, `i64` | Two's-complement, by width in bits. |
| Unsigned | `u8`, `u16`, `u32`, `u64` | Same widths, no sign bit. |

`i32` is the type your `main` returns. The other types come into play
when you start writing functions of your own and when you need to talk
to memory directly.

Try a literal that fits comfortably in `i32`:

```rust
fn main() -> i32 {
    return 100;
}
```

```text
100
```

Try one that does not. The literal `99999999999` is bigger than `i32`
can hold:

```rust
fn main() -> i32 {
    return 99999999999;
}
```

The compiler will reject this program with a diagnostic that points at
the literal and explains that the value does not fit. The type of `main`
is part of the contract: when you say `-> i32`, you have promised the
return value fits in 32 bits.

### Booleans

`bool` has exactly two values, `true` and `false`. You will use it
constantly once you have conditions in Chapter 4. For now, you can
return one cast to `i32`:

```rust
fn main() -> i32 {
    return true as i32;
}
```

```text
1
```

Change `true` to `false` and rerun:

```rust
fn main() -> i32 {
    return false as i32;
}
```

```text
0
```

`true` becomes `1` and `false` becomes `0`. That conversion is the only
way to turn a `bool` into an integer; Rexy will not do it implicitly.

### Characters

`char` holds a single character, written between single quotes. Every
character has a numeric code point you can see by casting:

```rust
fn main() -> i32 {
    return 'A' as u32 as i32;
}
```

```text
65
```

`'A'` is the character literal. `'A' as u32` is its code point as an
unsigned 32-bit integer. The second `as i32` brings it back into the
return type. Try a few:

```rust
fn main() -> i32 {
    return '0' as u32 as i32;
}
```

```text
48
```

```rust
fn main() -> i32 {
    return '\n' as u32 as i32;
}
```

```text
10
```

Character literals support a small set of escapes: `\n`, `\r`, `\t`,
`\'`, `\"`, and `\\`. Each escape produces one character.

### Strings

`str` holds an immutable string, written between double quotes:

```rust
let greeting: str = "hello";
```

You will see `str` used heavily in Chapter 13, where the standard
library tour shows you how to print and parse strings. For now, the
takeaway is that string literals exist and they have a type the language
can talk about.

### Casts With `as`

Rexy converts between primitive types only when you ask it to. The
operator is `as`. You have already used it: `true as i32`, `'A' as u32`.
The full set of legal casts on the current frontend covers
integer-to-integer, `bool`-to-integer, and `char as u32`.

A narrowing cast truncates the high bits:

```rust
fn main() -> i32 {
    return 300 as u8 as i32;
}
```

```text
44
```

`300` does not fit in a `u8` (which holds `0` to `255`), so the cast
keeps the lowest 8 bits: `300 - 256 = 44`. Rexy did exactly what you
asked. It did not warn you, because you used the cast operator on
purpose.

A widening cast preserves the value, sign-extending for signed types and
zero-extending for unsigned ones:

```rust
fn main() -> i32 {
    return -1 as i8 as i32;
}
```

```text
255
```

That number is the lowest byte of `-1` reported by your shell, which
matches Chapter 1's discussion of how exit statuses wrap to one byte.
Inside the program the value is genuinely `-1`; the operating system
reports the lowest byte to the shell.

### Where You Are by the End of Chapter 2

You can write integer, boolean, character, and string literals. You
have seen what happens when an integer literal does not fit, and you
have used `as` to convert between primitive types on purpose.

You know:

- Rexy has signed and unsigned integers in widths of 8, 16, 32, and 64
  bits.
- `bool` is `true` or `false` and converts to `1` or `0` with `as`.
- `char` holds a single character; `as u32` gives its code point.
- `str` holds a string literal; you will use it for real in Chapter 13.
- `as` is the only way to convert between primitive types, and the
  compiler does not insert one for you.

What you cannot do yet is hold a value in a name. In Chapter 3 you will
meet `let`, mutation with `mut`, and the rest of what it takes to write
a function with internal state.
