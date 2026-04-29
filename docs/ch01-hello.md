\newpage

## Chapter 1 - The First Rexy Program

### The Smallest Program That Runs

Every language has a smallest program that does something observable. In
Rexy, that program is a single function:

```rust
fn main() -> i32 {
    return 0;
}
```

Save it as `hello.rx`. The `.rx` extension is what the compiler looks for. The
file holds one **item**, which is Rexy's word for the things that can sit at
the top level of a source file: functions, structs, enums, statics, modules,
and a few others. We will meet each of those in time. For now we only need a
function.

`fn` introduces a function definition. The name `main` is special: it is the
entry point Rexy expects every executable program to provide. The `-> i32`
says that this function returns a 32-bit signed integer. The body is delimited
by braces, and inside the braces, `return 0;` hands a value back to whoever
called the function.

When this program runs on Drunix, the operating system reads that integer as
the program's exit status. Returning `0` is the conventional way to say "the
program finished without an error". Any other value signals a problem the
caller might want to know about.

### Building It

Once `hello.rx` exists, the compiler turns it into something Drunix can run.
The exact commands belong in the final chapter, where we cover the full path
from source to executable. For now, it is enough to know that `rexc` is the
compiler and that it consumes `.rx` files.

We will run programs throughout the book. When we do, the question we will
ask is not "what did the compiler do?" but "what did the program compute?".
Rexy is small enough that we can usually answer that by reading the source.

### Statements End in Semicolons

Notice the semicolon at the end of `return 0;`. Rexy is a statement-oriented
language: the body of a function is a sequence of statements, each terminated
by a semicolon. A statement is an instruction the program executes in order.
We will meet the full statement list as we go: `let`, `return`, assignment,
calls, `if`, `match`, `while`, `for`, `break`, `continue`, and a few others.

Braces group statements into a **block**. A function body is one block. So
are the bodies of `if`, `while`, `for`, and `match` arms. Blocks introduce
their own scope, which we will care about as soon as we have local variables
to talk about.

### A Slightly Bigger First Program

Returning a constant is not very interesting. We can do a little more before
we even leave Chapter 1:

```rust
fn main() -> i32 {
    return 1 + 2;
}
```

The expression `1 + 2` is evaluated, the result is `3`, and `3` is what the
program returns. Expressions are the right-hand side of statements. They
produce values. Rexy's expression grammar covers the things any C-family
programmer expects: integer literals, arithmetic, comparison, boolean
operators, parentheses, function calls, and a handful of other forms we will
introduce as we need them.

### Where We Are by the End of Chapter 1

We have written a Rexy file. We know that every program needs a `main`
function returning `i32`. We know that statements end in semicolons, that
blocks are delimited by braces, and that the value `main` returns becomes the
program's exit status on Drunix.

That is enough vocabulary to start asking what kinds of values Rexy can hold
and how the language describes them. Chapter 2 takes that question on
directly.
