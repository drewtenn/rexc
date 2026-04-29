\newpage

## Chapter 12 - Generics

### Why Generics

Suppose you want a function that swaps the components of a pair. For
`(i32, i32)`, the body is straightforward:

```rust
fn swap_i32(pair: (i32, i32)) -> (i32, i32) {
    return (pair.1, pair.0);
}
```

For `(bool, bool)`, the body is the same but with the type changed
everywhere:

```rust
fn swap_bool(pair: (bool, bool)) -> (bool, bool) {
    return (pair.1, pair.0);
}
```

You can write that once with a placeholder name where the type used to
be:

```rust
fn swap<T>(pair: (T, T)) -> (T, T) {
    return (pair.1, pair.0);
}

fn main() -> i32 {
    let ints: (i32, i32) = swap((1, 2));
    let flags: (bool, bool) = swap((false, true));
    return ints.0 + ints.1 + flags.0 as i32 + flags.1 as i32;
}
```

```text
4
```

The angle brackets after the function name introduce a list of **type
parameters**. Inside the body, `T` is a type. Wherever a parameter,
return type, or local declaration would name a concrete type, it can
name `T` instead. When the function is called, the compiler picks a
specific type for `T` and emits a copy of the body specialised for
that type.

This is called **monomorphization**: each distinct combination of type
parameters becomes its own dedicated function in the final program.
You see one generic function in the source. The linker sees one
specialised function per type your program actually uses.

### Generic Functions

A generic function has its type parameters declared between the name
and the parameter list. Try one over slices:

```rust
fn first_or<T>(values: &[T], fallback: T) -> T {
    if len(values) > 0 {
        return values[0];
    }
    return fallback;
}
```

Inside the body, `T` is a single, consistent type for the duration of
one call. A caller of `first_or` for a slice of `i32` and an `i32`
fallback gets an `i32` back. A caller for a slice of `bool` and a
`bool` fallback gets a `bool` back. The compiler enforces consistency
at the call site: calling `first_or` with a `&[i32]` and a `bool`
fallback is a compile error.

There is no syntax for naming the type parameter at the call site.
Rexy infers each type parameter from the argument types and the
expected return type.

### Generic Structs

Structs may also take type parameters:

```rust
struct Pair<T> {
    first: T,
    second: T,
}

fn main() -> i32 {
    let coords: Pair<i32> = Pair { first: 30, second: 12 };
    return coords.first + coords.second;
}
```

```text
42
```

A `Pair<T>` is a struct with two fields of the same type. The type
parameter `T` is part of the type's name. You write `Pair<i32>`,
`Pair<bool>`, or `Pair<char>` whenever you need to refer to a specific
instantiation.

### Multiple Type Parameters

A function or struct may take more than one type parameter. The list
is comma-separated:

```rust
struct Either<L, R> {
    left: L,
    right: R,
}

fn first_of<L, R>(pair: Either<L, R>) -> L {
    return pair.left;
}

fn main() -> i32 {
    let p: Either<i32, bool> = Either { left: 42, right: false };
    return first_of(p);
}
```

```text
42
```

Each parameter is independent. A program may instantiate `Either<i32,
bool>`, `Either<bool, i32>`, or `Either<i32, i32>`, and each is a
distinct type.

### Standard Library Types Are Generic

Two of the most useful types in the standard library are generic.
`Option` represents an optional value, and `Result` represents a value
that is either a success or a failure:

```rust
fn lookup(key: i32) -> Option<i32> {
    if key == 1 {
        return Some(42);
    }
    return None;
}

fn main() -> i32 {
    match lookup(1) {
        Some(value) => { return value; },
        None => { return 0; },
    }
}
```

```text
42
```

`Option<T>` has the variants `Some(T)` and `None`. `Result<T, E>` has
the variants `Ok(T)` and `Err(E)`. Both are checked the same way as
any other enum, with `match` arms that bind the carried value.

The `?` operator is a small piece of syntax for propagating an `Err`
up the call stack: applied to a `Result`, it unwraps an `Ok` value
and returns the `Err` from the enclosing function on failure. You
will see it sparingly in the book, but it is the standard idiom for
chaining fallible calls.

### Vec Of Anything

Generics carry their weight in the standard collections. A generic
`Vec<T>` lays out a backing array of `T`s and exposes the usual
push/get operations:

```rust
struct Vec<T> {
    data: *T,
    len: i32,
    capacity: i32,
}

unsafe fn vec_push<T>(v: *Vec<T>, value: T) -> i32 {
    if (*v).len >= (*v).capacity { return 0; }
    let slot: *T = (*v).data + (*v).len;
    *slot = value;
    (*v).len = (*v).len + 1;
    return 1;
}

unsafe fn vec_get<T>(v: *Vec<T>, index: i32) -> T {
    return *((*v).data + index);
}
```

The same template instantiates separately for every concrete element
type the program names. `Vec<i32>` and `Vec<Pair>` produce two
distinct mangled symbols at link time — neither knows the other
exists, neither can be passed where the other is expected. The body
inside `vec_push` reads as if it were written for one specific type;
the compiler stamps out one specialised copy per use.

Pointer arithmetic on `*T` scales by the size of `T`. Inside a
generic body, the compiler does not know the size up front, so each
monomorph is laid out and emitted with the size it needs. The same
source produces an `i32`-sized stride for `Vec<i32>` and an
8-byte stride for `Vec<Pair>` (where `Pair` is a two-`i32` struct on
arm64).

### Two Type Parameters: HashMap

A generic struct may take more than one type parameter. The plan is
the same; the layout has more fields:

```rust
struct HashMap<K, V> {
    keys: *K,
    values: *V,
    occupied: *u8,
    len: i32,
    capacity: i32,
}
```

Instantiation joins the type arguments at the link layer. `HashMap<i32,
str>` mangles as `HashMap__i32__str`. `HashMap<i32, bool>` and
`HashMap<u8, str>` are independent types; the compiler emits one
struct identity per concrete pair you actually use.

Operations on `HashMap<K, V>` cannot be fully generic in v1 because
the language has no way to ask "how do I hash a `K`?" or "how do I
compare two `K`s?" without a trait system or first-class function
values. So the operations ship as concrete per-type helpers:
`hashmap_i32_str_insert`, `hashmap_i32_str_lookup`, and so on. A
later release will revisit this when the language grows the dispatch
mechanism that lets generic ops call into the right hash and equality
functions for their type parameters.

### What Monomorphization Costs

Each instantiation of a generic produces a separate compiled function.
A program that calls `swap` for both `(i32, i32)` and `(bool, bool)`
ends up with two specialised copies in the final binary. That is the
cost of erasure-free generics. The benefit is that every instantiation
is fully type-checked and runs at the speed of code you would have
written by hand.

For most programs the cost is small. For systems-language programs,
where a small amount of duplication is cheap and a missing type
guarantee is expensive, the trade is firmly worth it.

### Where You Are by the End of Chapter 12

You can write functions and types once and use them across multiple
concrete types. Combined with modules, you have the tools to organise
a Rexy program of meaningful size and to share code within it.

You know:

- `fn name<T>(...) -> T { ... }` declares a generic function.
- `struct Name<T> { ... }` declares a generic struct.
- Type parameters are inferred at the call site; you do not write them
  explicitly.
- `Option<T>` and `Result<T, E>` are generic enums in the standard
  library.
- `Vec<T>` and `HashMap<K, V>` are generic structs that ship as
  templates. Their concrete operations are emitted per type
  combination: `Vec<i32>` and `Vec<bool>` are independent symbols at
  link time.

Part VI takes everything you have built and turns it outward. The
standard library gives you I/O, parsing, and string helpers. The final
chapter walks the program from a `.rx` source file to a runnable
executable on disk.
