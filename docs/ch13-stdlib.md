\newpage

## Chapter 13 - A Tour of the Standard Library

### What the Prelude Gives Us

Rexy ships with a small set of functions that are available in every
program without an explicit `use`. This is the **prelude**: the names that
form the everyday vocabulary of Rexy code. The prelude focuses on the
things almost every program needs: console I/O, integer formatting, string
inspection, and a panic helper for unrecoverable errors.

We will tour the prelude by category. None of these functions need any
import. They can be called from `main` and from any other function, just
like a function we declared ourselves.

### Console Output

Writing text to the console covers the largest share of stdlib calls in a
typical program. The prelude provides a complementary pair of functions
for plain text:

```rust
print("hello, ");
println("rexy");
```

`print` writes its argument to standard output without a trailing
newline. `println` writes its argument followed by `\n`. Both take a
`str`. Both return an `i32` status code, which most programs ignore.

For values other than strings, the prelude has typed helpers:

```rust
print_i32(42);
println_i32(-7);
print_bool(true);
println_bool(false);
print_char('!');
println_char('?');
```

Each helper formats its argument and writes it to standard output.
Programs that need richer formatting compose these helpers with explicit
casts and string concatenation built on slices and the underlying buffer
helpers.

### Console Input

Reading text from standard input uses two prelude functions:

```rust
let line: str = read_line();
let value: i32 = read_i32();
let flag: bool = read_bool();
```

`read_line` reads one line of input and returns it as a `str`. The
trailing newline, if present, is stripped. The returned value points into
a Rexy-owned buffer that is overwritten on the next call to `read_line`,
so a program that needs to keep multiple lines should copy the contents
to its own storage.

`read_i32` reads one line and parses it as a signed decimal integer.
`read_bool` reads one line and parses it as `true` or `false`. Both fall
back to default values for malformed input until the parsing helpers
return typed results.

### Parsing

Independent of input, the prelude exposes string-to-value parsers:

```rust
let value: i32 = parse_i32("-7");
let flag: bool = parse_bool("true");
```

`parse_i32` accepts an optional leading `-` followed by decimal digits.
Empty strings, invalid characters, and overflow currently return `0`.
`parse_bool` accepts the literal string `"true"` and returns `false` for
anything else. Both behave the same way in scripted contexts and in
interactive ones.

### Strings

A small family of string predicates and helpers covers the most common
inspection needs:

```rust
str_eq("rexc", "rexc");
str_starts_with("rexc", "re");
str_ends_with("rexc", "xc");
str_contains("rexc", "ex");
str_find("rexc", "ex");
str_is_empty("");
strlen("hello");
```

`str_eq` compares two strings for byte equality. `str_starts_with`,
`str_ends_with`, and `str_contains` test for prefixes, suffixes, and
substrings. `str_find` returns the byte index of a substring, or `-1` if
the substring does not appear. `str_is_empty` reports whether a string
has length zero. `strlen` returns the number of bytes before a null
terminator.

These helpers operate on null-terminated byte strings. They give us
enough machinery to check user input, dispatch on a flag, or build small
text-driven utilities, without pulling in heavier string types.

### Panicking

Some errors are unrecoverable. A program that encounters one calls
`panic`:

```rust
panic("something went wrong");
```

`panic` writes `panic: ` followed by the message, then terminates the
program with status `101`. It returns `i32` for compatibility with
expression contexts, but the program does not continue past the call.

### Generic Stdlib Types

Two generic types from Chapter 12 deserve a second mention here, because
they are part of the standard library surface even though their syntax
came up earlier. `Option<T>` represents an optional value with variants
`Some(T)` and `None`. `Result<T, E>` represents a fallible computation
with variants `Ok(T)` and `Err(E)`. Both are matched the way any enum is
matched. Both interact with the `?` operator for `Result` propagation
through fallible call chains.

### Beyond the Prelude

Names outside the default prelude are reachable through fully qualified
paths such as `std::io::println` or `std::process::exit`. These bridge-
backed declarations exist for code that wants explicit module discipline,
or for compiler internals and tests that opt into broader access. For
most programs, the prelude is enough.

### Where We Are by the End of Chapter 13

We can read input, write output, parse a few common formats, inspect
strings, and signal an unrecoverable error. Combined with everything from
Parts I through V, we now have the whole language and the standard tools
the language ships with.

The last thing we have not done is take a Rexy program and turn it into
something the operating system can run. That is Chapter 14.
