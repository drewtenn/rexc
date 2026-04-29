\newpage

## Chapter 13 - A Tour of the Standard Library

### What The Prelude Gives You

Rexy ships with a small set of functions that are available in every
program without an explicit `use`. This is the **prelude**: the names
that form the everyday vocabulary of Rexy code. The prelude focuses
on the things almost every program needs: console I/O, integer
formatting, string inspection, and a panic helper for unrecoverable
errors.

This chapter is the first one whose programs print to standard output.
You can keep watching exit statuses as before, but most of the time
you will care about what shows up on the terminal.

### Hello, Output

Try a real "hello, world":

```rust
fn main() -> i32 {
    println("hello, rexy");
    return 0;
}
```

Build and run:

```sh
./hello
```

```text
hello, rexy
```

`println` writes its argument to standard output followed by a
newline. It returns an `i32` status code, which most programs ignore.
The companion function `print` writes without the newline:

```rust
fn main() -> i32 {
    print("hello, ");
    print("rexy");
    println("");
    return 0;
}
```

```text
hello, rexy
```

### Printing Other Types

For values other than strings, the prelude has typed helpers:

```rust
fn main() -> i32 {
    print_i32(42);
    println("");
    println_i32(-7);
    println_bool(true);
    println_bool(false);
    println_char('!');
    println_char('?');
    return 0;
}
```

```text
42
-7
true
false
!
?
```

Each helper formats its argument and writes it to standard output.
The `_i32`, `_bool`, and `_char` suffixes select the type. Each one
has both a `print_*` form (no trailing newline) and a `println_*` form
(trailing newline).

### Reading Input

Reading text from standard input uses three prelude functions:

```rust
fn main() -> i32 {
    println("name?");
    let name: str = read_line();
    print("hello, ");
    println(name);
    return 0;
}
```

Run with input piped in:

```sh
echo "rexy" | ./hello
```

```text
name?
hello, rexy
```

`read_line` reads one line of input and returns it as a `str`. The
trailing newline is stripped. The returned value points into a
Rexy-owned buffer that is overwritten on the next `read_line` call,
so a program that needs to keep multiple lines should copy the
contents to its own storage.

`read_i32` reads one line and parses it as a signed decimal integer:

```rust
fn main() -> i32 {
    println("number?");
    let value: i32 = read_i32();
    print("doubled: ");
    println_i32(value + value);
    return 0;
}
```

```sh
echo "21" | ./hello
```

```text
number?
doubled: 42
```

`read_bool` reads one line and parses it as `true` or `false`. Both
fall back to default values for malformed input until the parsing
helpers return typed results.

### Parsing

Independent of input, the prelude exposes string-to-value parsers:

```rust
fn main() -> i32 {
    let value: i32 = parse_i32("-7");
    let flag: bool = parse_bool("true");
    println_i32(value);
    println_bool(flag);
    return 0;
}
```

```text
-7
true
```

`parse_i32` accepts an optional leading `-` followed by decimal
digits. Empty strings, invalid characters, and overflow currently
return `0`. `parse_bool` accepts the literal string `"true"` and
returns `false` for anything else.

### String Predicates

A small family of string predicates and helpers covers the most common
inspection needs:

```rust
fn main() -> i32 {
    println_bool(str_eq("rexc", "rexc"));
    println_bool(str_starts_with("rexc", "re"));
    println_bool(str_ends_with("rexc", "xc"));
    println_bool(str_contains("rexc", "ex"));
    println_i32(str_find("rexc", "ex"));
    println_bool(str_is_empty(""));
    println_i32(strlen("hello"));
    return 0;
}
```

```text
true
true
true
true
1
true
5
```

`str_eq` compares two strings for byte equality. `str_starts_with`,
`str_ends_with`, and `str_contains` test for prefixes, suffixes, and
substrings. `str_find` returns the byte index of a substring, or `-1`
if the substring does not appear. `str_is_empty` reports whether a
string has length zero. `strlen` returns the number of bytes before a
null terminator.

These helpers operate on null-terminated byte strings. They give you
enough machinery to check user input, dispatch on a flag, or build
small text-driven utilities, without pulling in heavier string types.

### Panicking

Some errors are unrecoverable. A program that hits one calls `panic`:

```rust
fn main() -> i32 {
    panic("something went wrong");
    return 0;
}
```

```text
panic: something went wrong
```

The shell will report exit status `101`. `panic` writes `panic: `
followed by the message, then terminates the program. It returns `i32`
for compatibility with expression contexts, but the program does not
continue past the call. The trailing `return 0;` in the example is
unreachable; it is there so the compiler accepts the function shape.

### Allocating With An Arena

Long-lived programs need somewhere to put values whose lifetime
outlives a single function. Rexy's standard allocator is an **arena**:
a chunk of memory the program owns, allocated linearly, freed all at
once. The prelude exposes the `Arena` struct and a small set of
operations on it:

```rust
static mut USER_BUF: [u8; 256];

fn main() -> i32 {
    let mut a: Arena = Arena {
        storage: USER_BUF + 0,
        capacity: 256,
        offset: 0,
    };
    arena_init(&a, USER_BUF + 0, 256);

    let block: *u8 = arena_alloc(&a, 12);
    return arena_used(&a);
}
```

```text
12
```

`arena_init` points the arena at a caller-supplied byte buffer.
`arena_alloc` carves `n` bytes off the front and returns a `*u8`
pointer into the buffer. `arena_used` reports how many bytes have
been handed out so far. `arena_remaining`, `arena_capacity`,
`arena_can_allocate`, and `arena_reset` round out the API.

The arena does not free individual allocations. Calling `arena_reset`
returns every byte at once, which is the entire point — for a phase
of a program that allocates many short-lived values, an arena is
faster than per-allocation tracking and never fragments.

For programs that want a single global allocator, the prelude exposes
`alloc_bytes`, `alloc_remaining`, and `alloc_reset` that operate on
an implicit static arena. These are the same operations against a
default-supplied buffer; a program that wants to route all of its
allocations through one place can use them without writing the
arena setup boilerplate.

### Hashing And Equality

For collections that key on values, the standard library provides a
small set of hash helpers under the prelude:

```rust
fn main() -> i32 {
    let h: u32 = hash_combine(2166136261, hash_i32(7));
    return h as i32;
}
```

`hash_combine` mixes a running hash seed with one new value.
Per-primitive helpers — `hash_i32`, `hash_u32`, `hash_bool`,
`hash_char`, `hash_str` — produce a `u32` hash for one value of that
type. To hash a struct of primitives, write a per-struct helper that
threads `hash_combine` through the fields:

```rust
struct Point {
    x: i32,
    y: i32,
}

fn point_hash(p: Point) -> u32 {
    let mut h: u32 = 2166136261;
    h = hash_combine(h, hash_i32(p.x));
    h = hash_combine(h, hash_i32(p.y));
    return h;
}

fn main() -> u32 {
    return point_hash(Point { x: 7, y: 3 });
}
```

Equal `Point` values produce equal hashes; unequal values produce
different hashes (with extremely high probability). The exit code
will be a moderately large `u32`; what matters is that running the
program a second time on the same input gives the same result.

Equality on primitives is the language's `==` and `!=` operators.
For structs, write a per-struct equality function comparing fields,
the same shape as `point_hash`. A future release will let you derive
both functions from the struct declaration; until then, writing them
out makes the contract explicit.

### Beyond The Prelude

Names outside the default prelude are reachable through fully
qualified paths such as `std::io::println` or `std::process::exit`.
These bridge-backed declarations exist for code that wants explicit
module discipline, or for compiler internals and tests that opt into
broader access. For most programs, the prelude is enough.

### Where You Are by the End of Chapter 13

You can read input, write output, parse a few common formats, inspect
strings, and signal an unrecoverable error. Combined with everything
from Parts I through V, you have the whole language and the standard
tools the language ships with.

You know:

- `print`, `println`, `print_i32`, `println_i32`, `print_bool`,
  `println_bool`, `print_char`, `println_char` cover output.
- `read_line`, `read_i32`, `read_bool` cover input.
- `parse_i32`, `parse_bool` parse strings.
- `str_eq`, `str_starts_with`, `str_ends_with`, `str_contains`,
  `str_find`, `str_is_empty`, `strlen` cover string inspection.
- `Arena` plus `arena_init`, `arena_alloc`, `arena_used`,
  `arena_remaining`, `arena_capacity`, `arena_can_allocate`,
  `arena_reset` are the explicit allocator. `alloc_bytes`,
  `alloc_remaining`, and `alloc_reset` route through an implicit
  static arena.
- `hash_combine` plus `hash_i32`, `hash_u32`, `hash_bool`,
  `hash_char`, `hash_str` are the hash contract. Equality on
  primitives is the language's `==`; struct equality is a
  user-written field-wise comparison.
- `panic` terminates the program with a message and exit status `101`.

The last thing you have not done is take a Rexy program and turn it
into something the operating system can run, on purpose, with the
flags chosen for that target. That is Chapter 14.
