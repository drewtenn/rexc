\newpage

## Chapter 12 - Generics

### Why Generics

Suppose we want a function that swaps the components of a pair. For
`(i32, i32)`, the body is straightforward. For `(bool, bool)`, the body is
the same, but with the type changed everywhere. Rexy lets us write that
function once, with a placeholder name where the type used to be:

```rust
fn swap<T>(pair: (T, T)) -> (T, T) {
    return (pair.1, pair.0);
}
```

The angle brackets after the function name introduce a list of **type
parameters**. Inside the body of the function, `T` is a type. Wherever a
parameter, return type, or local declaration would name a concrete type,
it can name `T` instead. When the function is called, the compiler picks a
specific type for `T` and emits a copy of the body specialised for that
type.

This is **monomorphization**: each distinct combination of type parameters
becomes its own dedicated function in the final program. The user sees one
generic function in the source, and the linker sees one specialised
function per type the program actually uses.

### Generic Functions

A generic function has its type parameters declared between the name and
the parameter list:

```rust
fn first_or<T>(values: &[T], fallback: T) -> T {
    if len(values) > 0 {
        return values[0];
    }
    return fallback;
}
```

Inside the body, `T` is a single, consistent type for the duration of one
call. A caller of `first_or` for a slice of `i32` and an `i32` fallback
gets an `i32` back. A caller for a slice of `bool` and a `bool` fallback
gets a `bool` back. The compiler enforces consistency at the call site:
calling `first_or` with a `&[i32]` and a `bool` fallback is a diagnostic.

There is no syntax for naming the type parameter at the call site. Rexy
infers each type parameter from the argument types and the expected return
type. That inference is the one place the language does work the
programmer does not have to write down. It is local to the call and the
results are visible from the surrounding context.

### Generic Structs

Structs may also take type parameters:

```rust
struct Pair<T> {
    first: T,
    second: T,
}
```

A `Pair<T>` is a struct with two fields of the same type. The type
parameter `T` is part of the type's name. We say `Pair<i32>`, `Pair<bool>`,
or `Pair<char>` whenever we need to refer to a specific instantiation.

Building a value uses the struct literal we already know, with the
instantiation worked out from context:

```rust
let coords: Pair<i32> = Pair { first: 3, second: 4 };
let flags: Pair<bool> = Pair { first: true, second: false };
```

The annotation on the `let` binding pins the instantiation. Inside the
struct literal, the fields take whatever type that instantiation names.

### Multiple Type Parameters

A function or struct may take more than one type parameter. The list is
comma-separated:

```rust
struct Either<L, R> {
    left: L,
    right: R,
}

fn first_of<L, R>(pair: Either<L, R>) -> L {
    return pair.left;
}
```

Each parameter is independent. A program may instantiate `Either<i32,
bool>`, `Either<bool, i32>`, or `Either<i32, i32>`, and each is a distinct
type.

### Standard Library Types Are Generic

Two of the most useful types in the standard library are generic. `Option`
represents an optional value, and `Result` represents a value that is
either a success or a failure:

```rust
let maybe: Option<i32> = Some(42);
let outcome: Result<i32, MyErr> = Ok(7);
```

`Option<T>` has the variants `Some(T)` and `None`. `Result<T, E>` has the
variants `Ok(T)` and `Err(E)`. Both are checked the same way as any other
enum, with `match` arms that bind the carried value:

```rust
match maybe {
    Some(value) => { return value; },
    None => { return 0; },
}
```

The `?` operator is a small piece of syntax for propagating an `Err` up
the call stack: applied to a `Result`, it unwraps an `Ok` value and
returns the `Err` from the enclosing function on failure. We will use it
sparingly in the book, but it is the standard idiom for chaining fallible
calls.

### What Monomorphization Costs

Each instantiation of a generic produces a separate compiled function. A
program that calls `first_or` for both `&[i32]` and `&[bool]` ends up with
two specialised copies in the final binary. That is the cost of erasure-
free generics. The benefit is that every instantiation is fully type-
checked and runs at the speed of code we would have written by hand.

For most programs the cost is small. For systems-language programs, where
a small amount of duplication is cheap and a missing type guarantee is
expensive, the trade is firmly worth it.

### Where We Are by the End of Chapter 12

We can write functions and types once and use them across multiple
concrete types. Combined with modules from Chapter 11, we now have the
tools to organise a Rexy program of meaningful size and to share code
within it.

Part VI takes everything we have built and turns it outward. The standard
library gives us I/O, parsing, and string helpers. The final chapter
walks the program from a `.rx` source file to a Drunix executable on
disk.
