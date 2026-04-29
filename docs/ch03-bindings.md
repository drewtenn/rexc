\newpage

## Chapter 3 - Bindings and Functions

### `let` Binds A Name To A Value

So far every program you have written has fit in one expression. Real
programs hold values in names. In Rexy, you introduce a name with `let`:

```rust
fn main() -> i32 {
    let count: i32 = 7;
    return count;
}
```

```text
7
```

Every part of that line matters. `let` says you are introducing a new
local. `count` is the name. `: i32` is the type annotation. `= 7` is
the initial value. The semicolon ends the statement.

Rexy does not infer the type of a binding from its initial expression.
You write the type down. Try removing the type annotation:

```rust
fn main() -> i32 {
    let count = 7;
    return count;
}
```

The compiler will refuse this. The error message points at the binding
and tells you a type annotation is required. Put `: i32` back, and the
program compiles.

### Bindings Are Immutable Unless You Say Otherwise

A plain `let` binding cannot be reassigned. The value it holds at the
moment of declaration is the value it holds for the rest of the
function. Try reassigning:

```rust
fn main() -> i32 {
    let count: i32 = 0;
    count = count + 1;
    return count;
}
```

The compiler will reject the assignment. To allow reassignment, mark
the binding `mut`:

```rust
fn main() -> i32 {
    let mut count: i32 = 0;
    count = count + 1;
    count = count + 1;
    return count;
}
```

```text
2
```

The two assignments now compile. Most locals you write will not need
`mut`. The ones that do are labelled, so anyone reading the function
can see at a glance which names change over its lifetime.

### `++` And `--`

Rexy gives you two compact mutation forms inherited from the C family.
`++` and `--` increment and decrement an integer binding by one, either
as a prefix or a postfix:

```rust
fn main() -> i32 {
    let mut i: i32 = 0;
    ++i;
    i++;
    return i;
}
```

```text
2
```

You will use these inside loops where they read smoothly. They only
work on `mut` integer bindings.

### `let mut` Without An Initializer

So far every binding has had a value at the point it was declared.
Sometimes the value depends on a branch, and forcing a placeholder up
front reads worse than the program you actually meant to write. Rexy
lets you declare a `let mut` binding without an initializer, and then
assign to it on every path before reading it:

```rust
fn pick(c: bool) -> i32 {
    let mut x: i32;
    if c {
        x = 30;
    } else {
        x = -1;
    }
    return x;
}

fn main() -> i32 {
    return pick(true);
}
```

```text
30
```

Two things matter here. The form is `let mut name: type;` — the type
annotation is still required, the `=` and initializer drop. And the
binding **must** be `mut`; an immutable uninitialized local could
never be assigned later, so the compiler rejects it as unobservably
useless.

The compiler tracks which paths assign the binding before it is read.
This is called **definite-assignment analysis**. Try reading `x`
before assigning it on every path:

```rust
fn pick(c: bool) -> i32 {
    let mut x: i32;
    if c {
        x = 30;
    }
    return x;
}

fn main() -> i32 {
    return pick(false);
}
```

The compiler refuses, with a diagnostic that points at the `return x`
and says `x` may be uninitialized — the `else` branch did not assign
it. Rounding out the `if` with an `else` fixes the program.

A diverging branch — one that ends with `return`, `break`, or
`continue` — does not need to assign the binding. The compiler knows
the branch never reaches the post-merge:

```rust
fn pick(c: bool) -> i32 {
    let mut x: i32;
    if c {
        x = 5;
    } else {
        return 0;
    }
    return x;
}

fn main() -> i32 {
    return pick(true);
}
```

```text
5
```

`while` and `for` bodies do not propagate definite-assignment forward
— the body may not execute at all. A binding assigned only inside a
loop body is still possibly-uninitialized after the loop.

### Functions

You have been writing one function the whole time: `main`. Adding more
follows the same shape:

```rust
fn name(parameter_list) -> return_type {
    statements
}
```

Try defining a helper and calling it from `main`:

```rust
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() -> i32 {
    return add(20, 22);
}
```

```text
42
```

Each parameter is a name and a type, separated by a colon. The arrow
`->` introduces the return type. The body is a block of statements.
Calling a function uses parentheses and positional arguments.

The compiler checks every call against the function's signature: the
number of arguments, the type of each one, and the type the call site
expects to receive back. If you change one of the call's arguments to
the wrong type, the compiler tells you exactly which argument and what
type it expected.

### Every Function Must `return`

Rexy is explicit about how values leave functions. Every path through a
function must end in a `return`. Try this:

```rust
fn classify(value: i32) -> i32 {
    if value > 0 {
        return 1;
    }
}

fn main() -> i32 {
    return classify(5);
}
```

You will meet `if` properly in Chapter 4. The point of this example is
that the `if` has no `else`, so when `value` is not greater than zero
the function falls off the end. The compiler refuses to compile this
program. Add a fallback:

```rust
fn classify(value: i32) -> i32 {
    if value > 0 {
        return 1;
    }
    return 0;
}

fn main() -> i32 {
    return classify(5);
}
```

```text
1
```

Now every path returns. The function compiles, and `main` returns
whatever `classify` returned.

### Composing Functions

Once you can define functions, you can compose them. Try this:

```rust
fn double(value: i32) -> i32 {
    return value + value;
}

fn main() -> i32 {
    return double(double(10));
}
```

```text
40
```

`double(10)` returns `20`. `double(20)` returns `40`. The expression
nests the way it would in any C-family language.

### Where You Are by the End of Chapter 3

You can declare local variables, mark them mutable when you need to
reassign them, increment and decrement integer bindings, define
functions with typed parameters, return values from them, and call them
from other functions.

You know:

- `let` introduces a binding; `let mut` makes it reassignable.
- Type annotations are required. Rexy does not infer them.
- `++` and `--` mutate integer bindings by one.
- `let mut name: type;` (no initializer) declares a slot whose value
  must be assigned on every path before any read. The compiler
  rejects use-before-init.
- A function's signature names every parameter type and the return
  type.
- Every path through a function must end in a `return`.

What you cannot do yet is choose between paths or repeat work. Part II
adds both: Chapter 4 covers `if`, `while`, `for`, `break`, and
`continue`, and Chapter 5 covers `match`.
