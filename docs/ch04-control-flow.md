\newpage

## Chapter 4 - Conditions and Loops

### `if` And `else`

Try a program that picks a value based on a condition:

```rust
fn main() -> i32 {
    let value: i32 = 5;
    if value > 0 {
        return 1;
    }
    return 0;
}
```

```text
1
```

Change `value` to `-3`:

```rust
fn main() -> i32 {
    let value: i32 = -3;
    if value > 0 {
        return 1;
    }
    return 0;
}
```

```text
0
```

The condition between `if` and the opening brace must be a `bool`. Rexy
will not silently treat an integer as a condition. Try this:

```rust
fn main() -> i32 {
    let value: i32 = 1;
    if value {
        return 1;
    }
    return 0;
}
```

The compiler refuses, with a message that the condition is `i32`, not
`bool`. Write the comparison out explicitly:

```rust
if value != 0 {
```

The program compiles again.

`if` accepts an optional `else`, and the `else` may be either another
block or another `if`:

```rust
fn classify(value: i32) -> i32 {
    if value > 0 {
        return 1;
    } else if value < 0 {
        return -1;
    } else {
        return 0;
    }
}

fn main() -> i32 {
    return classify(-7) + classify(0) + classify(42) + 100;
}
```

```text
100
```

`classify(-7)` returns `-1`, `classify(0)` returns `0`, and
`classify(42)` returns `1`. `-1 + 0 + 1 + 100` is `100`.

### Boolean Operators

The comparison and boolean operators are the ones you expect: `==`,
`!=`, `<`, `<=`, `>`, `>=` for comparisons, and `&&`, `||`, `!` for
logical combination. `&&` and `||` short-circuit: the right operand is
only evaluated if the left did not already determine the result.

```rust
fn main() -> i32 {
    let left: bool = 1 < 2;
    let right: bool = false;
    if !right && (left || right) {
        return 1;
    }
    return 0;
}
```

```text
1
```

The full precedence is the conventional one: comparisons bind tighter
than `&&`, which binds tighter than `||`. Parentheses always work and
are encouraged whenever the precedence is not immediately obvious.

### `while` Loops

`while` repeats a block as long as a `bool` condition holds:

```rust
fn main() -> i32 {
    let mut value: i32 = 0;
    while value < 10 {
        value = value + 1;
    }
    return value;
}
```

```text
10
```

The condition is checked before each iteration, including the first. If
the condition is `false` to start with, the loop body never runs:

```rust
fn main() -> i32 {
    let mut value: i32 = 0;
    while value > 0 {
        value = value + 1;
    }
    return value;
}
```

```text
0
```

### `for` Loops

The `for` loop is the C-style three-part form. Try a counter that sums
the integers `0` through `7`:

```rust
fn main() -> i32 {
    let mut total: i32 = 0;
    for let mut i: i32 = 0; i < 8; i = i + 1 {
        total = total + i;
    }
    return total;
}
```

```text
28
```

The initializer can be a `let` statement or an assignment to an
existing binding. The condition is a `bool`. The increment is an
assignment or one of the `++` and `--` increment forms. The parts may
also be wrapped in parentheses:

```rust
for (let mut i: i32 = 0; i < 8; ++i) {
    total = total + i;
}
```

The two forms compile to the same loop. Use whichever reads better in
context.

### `break` And `continue`

Inside a loop, `break` exits the loop immediately and `continue` skips
to the next iteration:

```rust
fn main() -> i32 {
    let mut value: i32 = 0;
    while value < 10 {
        value = value + 1;
        if value == 3 {
            continue;
        }
        if value == 7 {
            break;
        }
    }
    return value;
}
```

```text
7
```

The loop reaches `value == 3`, skips the rest of that iteration, keeps
counting, and then `break` ends the loop the moment `value` hits `7`.

`break` and `continue` only make sense inside a loop. Using them
outside one is a compiler diagnostic, not a runtime error.

### Cleanup With `defer`

Some operations need a paired cleanup: open a resource, do work, then
release it. Writing the release call by hand at every exit point is
tedious and easy to forget when an early `return` slips in. Rexy's
`defer` statement registers a cleanup call that runs whenever the
enclosing block exits, on every path:

```rust
static mut LOG: i32 = 0;

fn cleanup() -> i32 {
    LOG = LOG + 1;
    return 0;
}

fn run(early: bool) -> i32 {
    defer cleanup();
    if early {
        return 7;
    }
    return 8;
}

fn main() -> i32 {
    run(true);
    run(false);
    return LOG;
}
```

```text
2
```

Each `run` call schedules `cleanup` to run when the function exits,
no matter which `return` it takes. After both calls, `LOG` is `2` —
the cleanup fired once per call.

Three rules govern `defer`:

- **Block-scoped.** A `defer` registered inside an `if`-arm fires
  when that arm exits, not when the surrounding function exits.
- **LIFO.** Multiple defers in the same block run in reverse
  registration order. The last `defer` runs first.
- **Fires on every exit path.** `return`, `break`, `continue`, and
  the `?` propagation operator all run the deferred cleanups before
  leaving the block.

A worked example combining all three:

```rust
static mut RESULT: i32 = 0;

fn add_a() -> i32 { RESULT = RESULT * 10 + 1; return 0; }
fn add_b() -> i32 { RESULT = RESULT * 10 + 2; return 0; }

fn run(go: bool) -> i32 {
    defer add_a();
    if go {
        defer add_b();
        return 7;
    }
    return 8;
}

fn main() -> i32 {
    run(true);
    run(false);
    return RESULT;
}
```

```text
211
```

Trace `run(true)` first. `defer add_a()` is registered in the
function-body scope. Inside the `if` block, `defer add_b()` is
registered in the if-then scope. The early `return 7` exits both
scopes, innermost first: `add_b` runs (last-in, first-out), then
`add_a`. `RESULT` advances `0 → 2 → 21`. Now `run(false)`. Only
`add_a` is registered (the if-then was never entered). The trailing
`return 8` runs it: `RESULT` advances `21 → 211`. The exit code is
`211`.

The body of a `defer` is a single function call. That keeps the
ordering rules simple — there is no nested control flow inside a
deferred call to reason about. For more elaborate cleanup, write a
helper function and `defer` that.

### Conditions Stay Boolean

It is worth saying once more, because it is the most common surprise
for programmers arriving from C: every condition in Rexy must already
be a `bool`. There is no implicit conversion from integer to boolean.
If you want "non-zero", you write `!= 0`. If you want "non-empty
string", you call a string predicate from Chapter 13. The language
always asks: what does this condition *mean*?

### Where You Are by the End of Chapter 4

You can branch on conditions, loop while a condition holds, count
through a range with a `for`, and use `break` and `continue` to shape
how a loop ends or skips iterations.

You know:

- Every condition must be a `bool`. Rexy will not coerce integers.
- `if`, `else if`, `else` cover choice over boolean conditions.
- `&&` and `||` short-circuit; `!` negates.
- `while` and `for` repeat. `break` exits, `continue` skips to the
  next iteration.
- `defer call();` schedules a cleanup that runs when the enclosing
  block exits — on every path, in LIFO order.

When the data you are choosing over has shape rather than a single
boolean condition, plain `if` chains start getting awkward. Chapter 5
introduces `match`, the construct Rexy gives you for that case.
