\newpage

## Chapter 10 - The `unsafe` Boundary

### What `unsafe` Means

Rexy uses the keyword `unsafe` to mark code that performs operations whose
correctness the compiler cannot check on its own. Inside an `unsafe`
context, the programmer takes responsibility for promises the language
itself does not enforce: that a pointer is valid, that an offset is in
bounds, that a buffer is the right size, that a mutable static is being
accessed at a moment when no other part of the program also needs it.

`unsafe` does not turn off the type system. Every type rule still applies.
What `unsafe` does is permit a specific, named set of operations:

- Dereferencing a pointer (`*p` to read, `*p = ...` to write).
- Indexing through a pointer (`p[i]`, `p[i] = ...`).
- Pointer arithmetic (`p + n`, `p - n`).
- Reading or writing a `static mut`.
- Calling a function declared `unsafe fn`.

Outside an `unsafe` context, those operations are diagnostics. Inside one,
they compile.

### `unsafe fn`

A function may declare itself `unsafe`:

```rust
unsafe fn store(target: *i32, value: i32) -> i32 {
    *target = value;
    return value;
}
```

The body of an `unsafe fn` is itself an unsafe context. The function may
freely use the operations listed above. The keyword in the signature is a
contract: callers are being told that calling this function correctly
requires more than the type system can confirm, so calling it must also
happen in an unsafe context.

A safe function, declared without `unsafe`, may not perform unsafe
operations. It also may not call an `unsafe fn`. The boundary is
symmetric: producers of unsafe operations and consumers of unsafe
functions both have to opt in.

### `unsafe { }` Blocks

Inside an otherwise safe function, an `unsafe { }` block opens a localised
unsafe context:

```rust
fn write_one(target: *i32) -> i32 {
    unsafe {
        *target = 1;
    }
    return 0;
}
```

The block is the smallest scope that needs to be unsafe. The surrounding
function remains safe and can be called from anywhere. The unsafe
operations are confined to the block.

Most pointer-using code in Rexy lives inside small `unsafe` blocks for
exactly this reason. The function presents a safe surface to the rest of
the program. The unsafe block is the visible spot where the programmer
took responsibility for a memory operation.

### A Concrete Example

The pointer manipulation we wrote in Chapter 9 needs an unsafe context to
compile. With the boundary made explicit, the example reads:

```rust
unsafe fn pointers() -> i32 {
    let mut value: i32 = 7;
    let p: *i32 = &value;
    *p = 9;
    *(p + 0) = *p + 1;
    return p[1 - 1];
}
```

The function is `unsafe fn`, so its whole body is an unsafe context. Every
dereference and every pointer-indexed access compiles because we are
inside that context. Callers of `pointers` will have to be unsafe in turn.

### Mutating a `static mut`

Mutable statics also live behind `unsafe`. Reading and writing them is one
of the operations the boundary covers:

```rust
static mut COUNTER: i32 = 1;

unsafe fn bump() -> i32 {
    COUNTER = COUNTER + 1;
    return COUNTER;
}
```

The reasoning is the same as for pointers: the language cannot prove that
a static mutation is well-timed with respect to whatever else might be
running. The keyword keeps the operation visible.

### Why The Rule Is Worth The Cost

Putting pointer access and `static mut` access behind `unsafe` does mean a
small amount of extra ceremony. The trade is that every place memory could
go wrong is named in the source. A reader auditing a Rexy program for
memory bugs only has to read the unsafe contexts. The rest of the language
is enforced by the type system.

That property gets more valuable, not less, as a program grows. The
operating system that hosts our programs has to deal with memory failures
constantly. The language that runs on top of it should not hide where those
failures could come from.

### Where We Are by the End of Chapter 10

We have the boundary that Part IV was building toward. Pointers and
statics are real, usable, full-strength tools, and the contexts that use
them are clearly marked. We can write low-level code without smearing
unsafe operations across the whole program.

Part V steps back from memory and looks at how Rexy organises the source
itself: modules, visibility, and generics. That is the toolkit we need to
go from "a single source file with a `main`" to "a program that grows
beyond one file".
