\newpage

## Chapter 10 - The `unsafe` Boundary

### What `unsafe` Means

Rexy uses the keyword `unsafe` to mark code that performs operations
whose correctness the compiler cannot check on its own. Inside an
`unsafe` context, you take responsibility for promises the language
itself does not enforce: that a pointer is valid, that an offset is in
bounds, that a buffer is the right size, that a mutable static is
being accessed at a moment when no other part of the program also
needs it.

`unsafe` does not turn off the type system. Every type rule still
applies. What `unsafe` does is permit a specific, named set of
operations:

- Dereferencing a pointer (`*p` to read, `*p = ...` to write).
- Indexing through a pointer (`p[i]`, `p[i] = ...`).
- Pointer arithmetic (`p + n`, `p - n`).
- Reading or writing a `static mut`.
- Calling a function declared `unsafe fn`.

Outside an `unsafe` context, those operations are diagnostics. Inside
one, they compile.

### See What Happens Without `unsafe`

Try writing the pointer example from Chapter 9 *without* the keyword:

```rust
fn write_through() -> i32 {
    let mut value: i32 = 7;
    let p: *i32 = &value;
    *p = 99;
    return value;
}

fn main() -> i32 {
    return write_through();
}
```

The compiler refuses, with diagnostics that point at `&value`, `*p =
99`, and the use of a pointer in a safe function. Each one names the
exact operation that is not allowed in safe code.

Now mark the function `unsafe`:

```rust
unsafe fn write_through() -> i32 {
    let mut value: i32 = 7;
    let p: *i32 = &value;
    *p = 99;
    return value;
}

fn main() -> i32 {
    return write_through();
}
```

The pointer operations now compile, but a new error appears: a safe
function (`main`) cannot call an `unsafe fn`. The boundary is
symmetric. Producers of unsafe operations and consumers of unsafe
functions both have to opt in.

### `unsafe` Blocks

Wrap the call inside an `unsafe { }` block:

```rust
unsafe fn write_through() -> i32 {
    let mut value: i32 = 7;
    let p: *i32 = &value;
    *p = 99;
    return value;
}

fn main() -> i32 {
    return unsafe { write_through() };
}
```

```text
99
```

The block is the smallest scope that needs to be unsafe. The
surrounding function (`main`) remains safe and can be called from
anywhere. The unsafe operations are confined to the block.

Most pointer-using code in Rexy lives inside small `unsafe` blocks for
exactly this reason. The function presents a safe surface to the rest
of the program. The unsafe block is the visible spot where you took
responsibility for a memory operation.

### `unsafe fn` Versus `unsafe { }`

You have two tools for opening an unsafe context, and they are good
for different jobs.

Use `unsafe fn` when the function fundamentally cannot be made safe.
The body is full of pointer operations. The arguments are pointers
that the caller has to validate. The whole purpose of the function is
to do something the language cannot check. `bump`, `write_through`, and
`pointer_math` from Chapter 9 are all candidates.

Use `unsafe { }` when only a small region of an otherwise safe function
needs the boundary opened. A safe function might do most of its work
in safe code and then dip into a single pointer write to update a
buffer. Wrapping just that write in an `unsafe { }` block keeps the
rest of the function callable from safe code.

### Mutating A `static mut`

Mutable statics also live behind `unsafe`. Reading and writing them is
one of the operations the boundary covers:

```rust
static mut COUNTER: i32 = 1;

fn current() -> i32 {
    return unsafe { COUNTER };
}

fn bump() -> i32 {
    unsafe {
        COUNTER = COUNTER + 1;
    }
    return current();
}

fn main() -> i32 {
    return bump() + bump();
}
```

```text
5
```

`bump` reads and writes `COUNTER` inside a small `unsafe { }` block,
then calls `current` (also a thin wrapper around `unsafe`) to read its
new value. The first call returns `2`, the second returns `3`, and
`2 + 3` is `5`. Both `current` and `bump` are safe functions; only the
specific reads and writes need the boundary.

### Why The Rule Is Worth The Cost

Putting pointer access and `static mut` access behind `unsafe` does
mean a small amount of extra ceremony. The trade is that every place
memory could go wrong is named in the source. A reader auditing a
Rexy program for memory bugs only has to read the unsafe contexts.
The rest of the language is enforced by the type system.

That property gets more valuable, not less, as a program grows. The
operating system that hosts your programs has to deal with memory
failures constantly. The language that runs on top of it should not
hide where those failures could come from.

### Where You Are by the End of Chapter 10

You have the boundary that Part IV was building toward. Pointers and
statics are real, usable, full-strength tools, and the contexts that
use them are clearly marked.

You know:

- `unsafe fn` declares a function that may use pointer ops and `static
  mut` access.
- `unsafe { }` opens a small unsafe context inside an otherwise safe
  function.
- A safe function may not perform unsafe operations or call an
  `unsafe fn` directly. The boundary works in both directions.

Part V steps back from memory and looks at how Rexy organises the
source itself: modules, visibility, and generics. That is the toolkit
you need to grow a program past one file.
