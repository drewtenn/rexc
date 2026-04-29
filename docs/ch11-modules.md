\newpage

## Chapter 11 - Modules and Visibility

### Why Modules

Once a program has more than a few dozen items, putting them all in one
file stops helping. We want to group related items together, give that
group a name, and let the rest of the program refer to its contents
through that name. A **module** is exactly that: a named namespace whose
contents the program can reference.

Modules also serve a second purpose. They are the unit Rexy uses to draw
visibility boundaries. By default, items are private to the module that
declared them. Items that other modules need to use are marked `pub`.

### Declaring a Module

There are two ways to declare a module in Rexy. The first is **inline**:
the module's body sits directly inside braces:

```rust
mod arithmetic {
    pub fn add(a: i32, b: i32) -> i32 {
        return a + b;
    }
}
```

The second is **file-backed**: the module is declared with a semicolon,
and its body lives in a file of the same name in the same directory:

```rust
mod math;
```

This declaration tells the compiler to look for `math.rx` next to the
current source file and treat its contents as the body of the `math`
module. File-backed modules are the form we will use most of the time. They
keep each module to its own file and let the source tree mirror the module
tree.

### Qualified Names With `::`

Once a module exists, items inside it are reachable through a **qualified
name**. The separator is `::`:

```rust
let result: i32 = math::add(20, 22);
let half: i32 = math::double(21);
```

Qualified names compose. A function inside a module inside a module is
reached through two `::` separators. The compiler resolves the chain by
walking the module tree from the current point, the same way a filesystem
path resolves directories.

### `use` Declarations

Writing `math::add` everywhere is fine for occasional calls. For code that
reaches into the same module repeatedly, a `use` declaration brings the
final name into the current scope:

```rust
mod math;
use math::add;

fn main() -> i32 {
    let imported: i32 = add(20, 22);
    let qualified: i32 = math::double(21);
    return imported + qualified - 42;
}
```

After the `use math::add;`, the bare name `add` refers to `math::add`.
Qualified calls remain available; the `use` does not hide them. We pick
whichever form reads better at the call site.

### Visibility With `pub`

By default, a function, struct, enum, or static declared inside a module
is private to that module. Code outside the module cannot call it, name
it, or import it. To make an item visible to other modules, we mark it
`pub`:

```rust
pub fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

pub fn double(value: i32) -> i32 {
    return add(value, value);
}
```

The same `pub` keyword applies to structs, enums, and statics. A `pub
struct` exposes the type and its fields. A `pub enum` exposes the type and
its variants. A `pub static` exposes the static for read access (and write
access too, if it is `static mut` and the caller is in an unsafe
context).

Visibility lets a module decide what its outside surface looks like.
Private helpers stay private. Public functions form the contract the rest
of the program relies on. We reach for this distinction the moment a
module grows beyond a single function.

### Symbol Names Follow the Module Path

A function declared as `pub fn add` inside `mod math;` is known throughout
the program as `math::add`. The compiler also derives a unique symbol name
for it that incorporates the module path, so two `add` functions in two
different modules do not collide at the linker level. The path-qualified
language name and the unique symbol name are two views of the same thing.

### Where We Are by the End of Chapter 11

We can split a Rexy program across multiple files, name the resulting
modules, and choose what each module exposes. We can call into other
modules through qualified names or through `use` declarations. We have
enough structure now to grow a Rexy codebase without it becoming a single
sprawling file.

What we still cannot do is write code that works for more than one
specific type. The next chapter introduces generics and lets us reuse a
function or a struct across the primitive types we already know.
