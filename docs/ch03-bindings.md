\newpage

## Chapter 3 - Bindings and Functions

### `let` Introduces a Local

A **binding** is a name attached to a value inside a function body. Rexy
introduces bindings with `let`:

```rust
let count: i32 = 0;
```

Every part of that line is required. The keyword `let` says we are
introducing a new local. The name `count` is the identifier we will use to
refer to the value. The type annotation `: i32` tells the compiler what the
binding holds. The right-hand side `= 0` is the initial value. The semicolon
ends the statement.

Type annotations are not optional. Rexy does not attempt to infer the type of
a binding from its initial expression. The trade-off is the one we discussed
in Chapter 2: a small amount of typing now buys a program that any reader can
follow without running the compiler in their head.

### Mutation Is Opt-In

A plain `let` binding cannot be reassigned. The value it holds at the moment
of declaration is the value it holds for the rest of the function. To create
a binding that can be reassigned, we mark it `mut`:

```rust
let mut count: i32 = 0;
count = count + 1;
count = count + 1;
```

Without `mut`, the second and third lines would be rejected. That refusal is
the language's quiet way of pointing out that mutation is a separate
capability from existence. Most locals do not need it. The ones that do are
labelled, so a reader scanning a function body can see at a glance which
names change over the course of the function.

Reassignment uses ordinary syntax: `count = count + 1;`. The form is the same
whether we are incrementing, recomputing, or replacing.

There are also two compact mutation forms inherited from the C family. `++`
and `--` increment and decrement an integer binding by one, either as a
prefix or a postfix:

```rust
let mut i: i32 = 0;
++i;
i++;
```

Both produce the same effect on `i`. We will use them inside loops where they
are conventional and read smoothly.

### Functions

We have already seen the shape of a function from Chapter 1. The general form
is:

```rust
fn name(parameter_list) -> return_type {
    statements
}
```

A function takes a comma-separated list of parameters, each of which is a
name and a type, and returns a value whose type is named after the `->`. The
body is a block of statements.

```rust
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn double(value: i32) -> i32 {
    return add(value, value);
}
```

Calling a function uses ordinary parentheses, with positional arguments. The
compiler checks that we passed the right number of arguments, that each one
has the type the parameter declared, and that the call site uses the return
value in a context that matches its type.

### `return` Is a Statement

Every function in Rexy must end its execution with a `return` statement. The
expression after `return` becomes the function's result. This is not the
same as some languages where the last expression of a block is implicitly
returned. Rexy is explicit: the only way a value leaves a function is through
a `return`.

When a function has multiple paths, every path must end in a `return`:

```rust
fn sign(value: i32) -> i32 {
    if value > 0 {
        return 1;
    } else if value < 0 {
        return -1;
    } else {
        return 0;
    }
}
```

If a path could fall off the end of a function without returning, the
compiler rejects the program. We never wonder what a Rexy function returned.
If it returned at all, it returned because of a `return`.

### Parameter and Return Types Cover the Whole Surface

A function's signature is its full external description. The type of every
parameter is in the signature. The return type is in the signature. There
are no implicit parameters, no hidden return paths, and no values that exist
outside the type system. When we read a Rexy function header, we are reading
the entirety of its contract with the rest of the program.

### Where We Are by the End of Chapter 3

We can declare local variables with `let`, mark them `mut` when we need to
reassign them, increment and decrement integers, define functions with typed
parameters and a typed return, and call functions positionally. We have the
ingredients for a useful straight-line program.

What we still lack is the ability to choose between paths and to repeat work.
Part II covers both: Chapter 4 brings in conditions and loops, and Chapter 5
brings in `match`, which is how Rexy expresses choice over structured data.
