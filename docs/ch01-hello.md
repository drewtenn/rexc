\newpage

## Chapter 1 - Your First Rexy Program

### Write It

Open a new file called `hello.rx`. Every Rexy source file ends in `.rx`.
That is what the compiler looks for, and it is the only file extension
this book will use.

Type this in:

```rust
fn main() -> i32 {
    return 0;
}
```

Save the file. You have just written a complete Rexy program. It does not
do much yet, but it is a real program: the compiler will accept it and
Drunix will run it.

### Build And Run It

From the directory where you saved `hello.rx`, run:

```sh
rexc hello.rx --target i386-drunix --drunix-root "$DRUNIX" -o hello.drunix
./hello.drunix
echo $?
```

Replace `$DRUNIX` with the path to your Drunix checkout. We will come
back to those flags in Chapter 14, where you will need to know what each
one is doing. For now, treat the command as the recipe for "compile and
link a Rexy program into a Drunix executable".

You should see this:

```text
0
```

That number is what your program returned to the operating system.
Drunix took the value out of `main` and reported it as the exit status.
The convention across most operating systems is that `0` means "the
program finished without error" and any other value means something the
caller might want to know about.

### Look At What You Wrote

Read the program again now that you have run it:

```rust
fn main() -> i32 {
    return 0;
}
```

Three things are worth naming.

`fn main` declares a function called `main`. Every Rexy executable needs
a `main`. When the operating system starts your program, it eventually
calls this function, and what `main` returns becomes the program's exit
status.

`-> i32` is the return type. The arrow says "this function returns",
followed by the type of value it returns. `i32` is a 32-bit signed
integer. You will meet the rest of the primitive types in Chapter 2, but
`i32` is the one you will see most often, and the one Drunix expects
from `main`.

`return 0;` is a statement. It hands the value `0` back as the
function's result. The semicolon ends the statement, the same way
semicolons end statements in C.

### Try Returning Something Else

Edit `hello.rx` and change the body to return `42`:

```rust
fn main() -> i32 {
    return 42;
}
```

Rebuild and run:

```sh
rexc hello.rx --target i386-drunix --drunix-root "$DRUNIX" -o hello.drunix
./hello.drunix
echo $?
```

```text
42
```

Drunix faithfully reported whatever number `main` handed it. Try a few
more values. Try `-1`:

```rust
fn main() -> i32 {
    return -1;
}
```

```text
255
```

That probably is not what you expected. Exit statuses on most operating
systems wrap to fit in a single byte, so `-1` comes back as `255`, and a
return of `256` would look like `0`. That is a Drunix and Unix
convention, not a Rexy rule. Your program faithfully returned the value
you typed; the shell only showed you the lowest byte.

### Try A Tiny Bit Of Math

You do not have to return a literal. You can return any expression of
the right type. Try this:

```rust
fn main() -> i32 {
    return 1 + 2;
}
```

```text
3
```

Now this:

```rust
fn main() -> i32 {
    return (10 - 3) * 2;
}
```

```text
14
```

Rexy gives you the arithmetic operators you expect from a C-family
language: `+`, `-`, `*`, `/`, and `%`. Parentheses group the way you
expect, too: in the program above, the subtraction happens before the
multiplication.

### See What Happens When You Get It Wrong

Try forgetting a semicolon:

```rust
fn main() -> i32 {
    return 0
}
```

Build it. The compiler should refuse, with a message that points at the
line where the semicolon was missing. Put it back, and the program
compiles again.

Now try forgetting the `return`:

```rust
fn main() -> i32 {
    0;
}
```

The compiler should refuse this one too. Every path through a Rexy
function must end in a `return`. The compiler enforces this so that no
function ever falls off the end without giving its caller a value back.

These error messages will be your most common companion as you learn
Rexy. They are designed to point at the exact problem and to tell you
what the language expected. Read them carefully when they appear.

### Where You Are by the End of Chapter 1

You have written a Rexy file, compiled it, run it on Drunix, and watched
the operating system report the value `main` returned. You also know
what happens when you forget a semicolon or a `return`.

You know:

- Rexy source files end in `.rx`.
- Every executable program needs a `main` function returning `i32`.
- Statements end in semicolons; bodies sit inside braces.
- The value `main` returns becomes the program's exit status.
- Arithmetic works the way you expect from any C-family language.

In Chapter 2 you will start putting more interesting values into your
programs. You will meet the full set of primitive types Rexy supports
and learn how to convert between them.
