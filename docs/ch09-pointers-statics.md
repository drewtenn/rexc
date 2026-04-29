\newpage

## Chapter 9 - Pointers and Statics

### Why Rexy Has Pointers

Most modern languages hide pointers. They give you references, garbage
collection, and value semantics, and they let you pretend addresses do
not exist. Rexy is a systems language and the operating system it
serves ultimately has to manipulate memory by address. So pointers
stay in the language, with a small but explicit syntax.

The examples in this chapter need an `unsafe` context to compile.
Chapter 10 explains the rule. For now, treat `unsafe fn` as the
keyword that lets a function perform pointer operations and read along.

### Taking An Address

A **pointer** is a value whose contents are an address. The type of a
pointer is written `*T`, where `T` is the type of value the pointer is
expected to point at:

```rust
unsafe fn through_pointer() -> i32 {
    let mut value: i32 = 7;
    let p: *i32 = &value;
    return *p;
}

fn main() -> i32 {
    return unsafe { through_pointer() };
}
```

```text
7
```

`&value` produces a pointer to `value`. The type of the pointer is
`*i32` because `value` is an `i32`.

### Reading Through A Pointer

`*p` reads the value at the address. The expression has the type the
pointer points at:

```rust
let here: i32 = *p;
```

You have just used this idiom in the example above, where `return *p;`
read the `i32` `value` was holding.

### Writing Through A Pointer

To write a new value, you assign to `*p`:

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

The same `*p` form means "the value at the address `p`" on both sides
of the assignment. On the right, it produces the value. On the left,
it names the place the value will be written. Writing through `p`
changes `value`, because `p` *is* the address of `value`.

### Indexing And Pointer Arithmetic

Pointers also support indexing. Given `p: *i32`, the expression `p[i]`
reads the `i`-th `i32` value starting from `p`. Writing to `p[i] = ...`
writes through that same offset. Indexing is exactly the same as
`*(p + i)`, written more readably:

```rust
unsafe fn pointer_math() -> i32 {
    let mut value: i32 = 7;
    let p: *i32 = &value;
    *p = 9;
    *(p + 0) = *p + 1;
    return p[1 - 1];
}

fn main() -> i32 {
    return unsafe { pointer_math() };
}
```

```text
10
```

Pointer arithmetic adds an integer offset, scaled by the size of the
pointee type. `p + 1` advances by one `i32`, not by one byte.

### When A Pointer Goes Wrong

Dereferencing an incorrect pointer is undefined: the value you read is
whatever happens to be at the address, and the write you issue might
land on memory that belongs to something else. Rexy does not check
pointer arithmetic at runtime. The compiler does not promise that a
`*i32` actually points at a live `i32`. That is part of the price of
access at this level.

The rest of the language is built around values that carry their own
meaning: a `bool` is always `true` or `false`, an enum is always one
of its variants. Pointers do not give you that guarantee. That
difference is why Rexy puts pointer use behind a fence, which Chapter
10 covers.

### `static` And `static mut`

A **static** is a value with program-lifetime storage. It exists from
program start to program exit, regardless of what functions are
running. Statics live at the top level of a file, alongside functions
and types:

```rust
static SCALE: i32 = 100;
static mut COUNTER: i32 = 1;

unsafe fn bump() -> i32 {
    COUNTER = COUNTER + 1;
    return COUNTER;
}

fn main() -> i32 {
    let first: i32 = unsafe { bump() };
    let second: i32 = unsafe { bump() };
    return first + second;
}
```

```text
5
```

`bump()` increments `COUNTER` and returns its new value. The first
call returns `2`. The second call returns `3`. `2 + 3` is `5`.

A plain `static` is read-only. Other code can read its value but
cannot assign to it. A `static mut` is mutable: the program may assign
to it during execution.

### Static Buffers

Statics also support a fixed-size buffer form, which is the
systems-language equivalent of a global array:

```rust
static mut SCRATCH: [u8; 16];
```

That declaration reserves a 16-byte buffer named `SCRATCH` whose
initial contents are zero. The buffer name, used in an expression,
evaluates to a pointer to its first element. Combined with the pointer
machinery from earlier in the chapter, you can read and write into the
buffer:

```rust
static mut SCRATCH: [u8; 16];

unsafe fn write_byte(index: i32, value: u8) -> i32 {
    let p: *u8 = (SCRATCH + index) as *u8;
    *p = value;
    return *p as i32;
}

fn main() -> i32 {
    let written: i32 = unsafe { write_byte(0, 42) };
    return written;
}
```

```text
42
```

You will not always need static buffers. When you do, this is the
shape they take.

### Where You Are by the End of Chapter 9

You can refer to memory by address, read and write through that
address, index across a run of typed values, and declare program-
lifetime storage that those pointers can target.

You know:

- `*T` is a pointer type; `&value` takes the address of a value.
- `*p` reads the value at `p`; `*p = ...` writes to it.
- `p[i]` indexes; `p + n` advances by `n` elements of the pointee
  type.
- `static` and `static mut` declare program-lifetime storage.
- Static buffer types like `[u8; 16]` reserve a fixed-size run.

What you have been ignoring all chapter is the keyword that made every
example here compile: `unsafe`. Chapter 10 explains the rule it
enforces and where it has to appear.
