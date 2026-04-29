\newpage

## Chapter 11 - Modules and Visibility

### Why Modules

Once a program has more than a few dozen items, putting them all in one
file stops helping. You want to group related items together, give that
group a name, and let the rest of the program refer to its contents
through that name. A **module** is a named namespace whose contents the
program can reference.

Modules also serve a second purpose. They are the unit Rexy uses to
draw visibility boundaries. By default, items are private to the
module that declared them. Items that other modules need to use are
marked `pub`.

### Inline Modules

The simplest form of a module sits directly inside braces. Try this in
a single file:

```rust
mod arithmetic {
    pub fn add(a: i32, b: i32) -> i32 {
        return a + b;
    }
}

fn main() -> i32 {
    return arithmetic::add(20, 22);
}
```

```text
42
```

The qualified name `arithmetic::add` reaches into the module and calls
the function. The `pub` keyword on `add` makes it visible to code
outside the module.

If you remove the `pub`:

```rust
mod arithmetic {
    fn add(a: i32, b: i32) -> i32 {
        return a + b;
    }
}
```

The compiler refuses to compile the call from `main`, because `add` is
now private to `arithmetic`. Visibility is not a hint; it is a checked
contract.

### File-Backed Modules

The form you will use most of the time is **file-backed**: the module
is declared with a semicolon, and its body lives in a file of the same
name in the same directory.

Create a file called `math.rx`:

```rust
pub fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

pub fn double(value: i32) -> i32 {
    return add(value, value);
}
```

Then a file called `main.rx` next to it:

```rust
mod math;

fn main() -> i32 {
    return math::add(20, 22);
}
```

Build `main.rx` the way you have been building everything else:

```sh
rexc main.rx --target arm64-macos -o app
./app
echo $?
```

```text
42
```

The compiler sees `mod math;`, looks for `math.rx` next to the source
file, and treats its contents as the body of the `math` module. Both
`add` and `double` are `pub`, so the module exports them. The private
helpers inside `math.rx` would not be reachable from `main.rx`.

Notice that `double` calls `add` without qualification. Inside
`math.rx`, both names are in the same module, so neither needs the
prefix. From `main.rx`, you have to write `math::add` because the call
is reaching across a module boundary.

### `use` Brings A Name Into Scope

Writing `math::add` everywhere is fine for occasional calls. For code
that reaches into the same module repeatedly, a `use` declaration
brings the final name into the current scope:

```rust
mod math;
use math::add;

fn main() -> i32 {
    let imported: i32 = add(20, 22);
    let qualified: i32 = math::double(21);
    return imported + qualified - 42;
}
```

```text
42
```

After the `use math::add;`, the bare name `add` refers to `math::add`.
Qualified calls remain available; the `use` does not hide them. Pick
whichever form reads better at the call site.

### What `pub` Covers

The same `pub` keyword applies to functions, structs, enums, and
statics:

- `pub fn` exposes a function.
- `pub struct` exposes a struct and its fields.
- `pub enum` exposes an enum and all of its variants.
- `pub static` exposes a static for read access (and write access too,
  if it is `static mut` and the caller is in an unsafe context).

Visibility lets a module decide what its outside surface looks like.
Private helpers stay private. Public functions form the contract the
rest of the program relies on. Reach for the distinction the moment a
module grows beyond a single function.

### Where You Are by the End of Chapter 11

You can split a Rexy program across multiple files, name the resulting
modules, and choose what each module exposes. You can call into other
modules through qualified names or through `use` declarations.

You know:

- `mod name { ... }` declares an inline module.
- `mod name;` declares a file-backed module that lives in
  `name.rx` next to the current file.
- `name::item` is a qualified path; `use name::item;` brings it into
  scope.
- `pub` exposes an item across module boundaries; without `pub`, it
  is private to its module.

What you still cannot do is write code that works for more than one
specific type. Chapter 12 introduces generics and lets you reuse a
function or a struct across the primitive types you already know.
