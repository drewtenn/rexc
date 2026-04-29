\newpage

## Chapter 9 - Pointers and Statics

### Why Rexy Has Pointers

Most modern languages hide pointers. They give us references, garbage
collection, and value semantics, and they let us pretend that addresses do
not exist. Rexy is a systems language and the operating system it serves
ultimately has to manipulate memory by address. So pointers stay in the
language, with a small but explicit syntax.

A **pointer** is a value whose contents are an address: it points at
another value somewhere in memory. The type of a pointer is written `*T`,
where `T` is the type of value the pointer is expected to point at:

```rust
let mut value: i32 = 7;
let p: *i32 = &value;
```

`&value` produces a pointer to `value`. The type of the pointer is `*i32`
because `value` is an `i32`. The pointer is a value just like any other:
we can pass it to a function, store it in a struct, or compare it to
another pointer.

### Reading and Writing Through a Pointer

To read the value a pointer points at, we **dereference** it with `*`:

```rust
let here: i32 = *p;
```

To write a new value through a pointer, we use `*p = ...`:

```rust
*p = 9;
```

The same `*p` form means "the value at the address `p`" on both sides of
the assignment. On the right, it produces the value. On the left, it names
the place the value will be written.

Pointers also support indexing. Given `p: *i32`, the expression `p[i]`
reads the `i`-th `i32` value starting from `p`. Writing to `p[i] = ...`
writes through that same offset. Indexing is exactly the same as
`*(p + i)`, written more readably:

```rust
*(p + 0) = *p + 1;
let next: i32 = p[1 - 1];
```

Pointer arithmetic adds an integer offset, scaled by the size of the
pointee type. `p + 1` advances by one `i32`, not by one byte.

### When a Pointer Goes Wrong

Dereferencing an incorrect pointer is undefined: the value we read is
whatever happens to be at the address, and the write we issue might land
on memory that belongs to something else. Rexy does not check pointer
arithmetic at runtime. The compiler does not promise that a `*i32` actually
points at a live `i32`. That is part of the price of access at this level.

The rest of the language is built around values that carry their own
meaning: a `bool` is always `true` or `false`, an enum is always one of its
variants. Pointers do not give us that guarantee. That difference is why
Rexy puts pointer use behind a fence, which we cover in Chapter 10.

### `static` and `static mut`

A **static** is a value with program-lifetime storage. It exists from
program start to program exit, regardless of what functions are running.
Statics live at the top level of a file, alongside functions and types:

```rust
static SCALE: i32 = 100;
static mut COUNTER: i32 = 1;
```

A plain `static` is read-only. Other code can read its value but cannot
assign to it. A `static mut` is mutable: the program may assign to it
during execution. The mutation has the same explicitness rule as a `mut`
local: if it is not labelled `mut`, it cannot be reassigned.

Statics are typed. The right-hand side of a static is a constant initial
value. For scalar statics that value is just a literal. Statics also
support a fixed-size buffer form, which is the systems-language equivalent
of a global array:

```rust
static mut SCRATCH: [u8; 16];
static GREETING: [u8; 5] = [104, 101, 108, 108, 111];
```

The first declaration reserves a 16-byte buffer named `SCRATCH` whose
initial contents are zero. The second declares a 5-byte buffer initialised
to the byte values shown. The buffer name, used in an expression, evaluates
to a pointer to its first element.

### Pointer Use Lives Behind `unsafe`

We have shown a lot of pointer manipulation above without explaining the
keyword most of those examples actually need: `unsafe`. The next chapter
explains the rule and why it is the rule.

### Where We Are by the End of Chapter 9

We have a way to refer to memory by address, to read and write through
that address, to index across a run of typed values, and to declare
program-lifetime storage that those pointers can target. Combined with
slices from Chapter 8, we now have the full machinery a systems-language
program uses to talk about memory.

What is missing is the explicit boundary the language draws between safe
code and the operations that touch memory directly. Chapter 10 introduces
`unsafe` and explains where it has to appear.
