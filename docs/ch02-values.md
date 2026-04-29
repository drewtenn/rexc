\newpage

## Chapter 2 - Values, Types, and Casts

### Why Types Are Written Down

Rexy is a typed language with no inference at the binding level. Every local
variable, every function parameter, every return type, and every static is
written down in the source. That is a deliberate cost. It buys two things
that matter for a systems language: the reader of a program never has to
guess what a name holds, and the compiler never has to guess either.

Types in Rexy describe what a value *is*, including how many bits it
occupies, whether it is signed, and whether the language treats it as a
number, a flag, a code point, or a string.

### The Primitive Types

Rexy ships with a small, regular primitive set:

| Family | Types | Notes |
| --- | --- | --- |
| Signed integers | `i8`, `i16`, `i32`, `i64` | Two's-complement, by width in bits. |
| Unsigned integers | `u8`, `u16`, `u32`, `u64` | Same widths, no sign bit. |
| Boolean | `bool` | Holds `true` or `false`. |
| Character | `char` | A single ASCII code point. |
| String | `str` | An immutable string slice. |

Signed and unsigned integers are different types. `i32` and `u32` both occupy
32 bits, but the language refuses to mix them in arithmetic without an
explicit conversion. Comparisons and division behave differently for signed
and unsigned values, and Rexy does not let those differences hide.

### Integer Literals

Integer literals are written in decimal and have no implicit type until they
are placed into a context that supplies one:

```rust
let small: i32 = 42;
let big: u64 = 18446744073709551615;
let negative: i64 = -9223372036854775808;
```

Each literal is checked against the type it lands in. The compiler keeps the
original decimal text of every literal so it can validate values that are too
large to fit in an intermediate signed integer during parsing. A literal that
will not fit in its declared type is rejected with a diagnostic at the
literal's source location.

### Booleans

`bool` has exactly two values, written `true` and `false`. Boolean values are
not integers, and the language will not let us treat them as numbers. The
condition of an `if`, a `while`, or a `for` must be a `bool`, not "anything
that is not zero". That rule is the same idea as written-down types: the
language refuses to guess what we meant.

### Characters and Strings

`char` is a single character literal, written between single quotes:

```rust
let letter: char = 'A';
let newline: char = '\n';
```

Character literals support a small set of escapes: `\n`, `\r`, `\t`, `\'`,
`\"`, and `\\`. Each escape produces one character.

`str` is an immutable string, written between double quotes:

```rust
let greeting: str = "hello";
```

Strings carry the same escapes as characters. We will say more about how
strings are represented when we reach pointers and slices, but for everyday
use a `str` is just a value we can pass around and hand to print functions.

### Casting With `as`

Rexy converts between primitive types only when we ask it to. The conversion
operator is `as`:

```rust
let code: u32 = 'A' as u32;
let truncated: u8 = 300 as u8;
let widened: i32 = (truncated as i32) + 1;
let flag: i32 = true as i32;
```

`as` performs the obvious primitive conversion: integers widen or narrow by
copying or truncating bits, signed integers extend their sign on widening,
unsigned integers fill with zeros, `char` values convert to the integer of
their code point, and `bool` converts to `0` or `1`. The rule is that every
conversion is local and visible: a reader can see exactly where a value
changed type.

### Where We Are by the End of Chapter 2

We have a vocabulary of values. We can write integer, boolean, character, and
string literals. We can declare a local with a type annotation, even if we
have not yet pinned down all the rules around what a `let` binding really
means. We can convert between primitive types with `as` whenever the program
needs to.

What we cannot do yet is compose those values into something a program can
*do*. Chapter 3 introduces bindings and functions, the two constructs that
turn isolated values into a working program.
