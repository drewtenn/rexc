\newpage

## Chapter 6 - Structs

### Declaring A Struct

A **struct** is a named record of fields. Each field has its own name
and its own type. You declare a struct at the top level of a file,
alongside functions:

```rust
struct Point {
    x: i32,
    y: i32,
}

fn main() -> i32 {
    let origin: Point = Point { x: 0, y: 0 };
    return origin.x + origin.y;
}
```

```text
0
```

The declaration creates a new type called `Point`. From this point on,
`Point` may appear anywhere a primitive type may appear: as a parameter
type, a return type, or the type of a `let` binding.

### Building A Struct Value

To build a value of a struct type, use a **struct literal**: name the
struct, then provide each field by name.

```rust
struct Point {
    x: i32,
    y: i32,
}

fn main() -> i32 {
    let p: Point = Point { x: 30, y: 12 };
    return p.x + p.y;
}
```

```text
42
```

Every field the struct declared must appear, and a field name may not
appear twice. The order does not have to match the declaration order,
but readers will thank you if it usually does.

If you forget a field, the compiler tells you which one is missing. Try
this:

```rust
let p: Point = Point { x: 30 };
```

The diagnostic names `y` as a missing field. Add it back, and the
program compiles.

### Reading Fields

Field access uses a dot:

```rust
let x: i32 = p.x;
let y: i32 = p.y;
```

A field expression has the type of the field. The same dot syntax works
inside any larger expression. There is nothing magical about it: it is
just the way to ask a struct value for one of its parts.

### Passing Structs To Functions

Struct values pass to functions like any other value. Pass them in by
type name and read the fields inside:

```rust
struct Point {
    x: i32,
    y: i32,
}

fn manhattan(p: Point) -> i32 {
    return p.x + p.y;
}

fn main() -> i32 {
    let here: Point = Point { x: 30, y: 12 };
    return manhattan(here);
}
```

```text
42
```

You can also return structs:

```rust
fn shifted(p: Point, dx: i32) -> Point {
    return Point { x: p.x + dx, y: p.y };
}

fn main() -> i32 {
    let p: Point = Point { x: 10, y: 20 };
    let q: Point = shifted(p, 5);
    return q.x + q.y;
}
```

```text
35
```

`shifted` builds and returns a fresh `Point`. The caller binds it to
`q` and reads its fields.

### Mutating Fields Through A Pointer

You will not see this form in full until Part IV, but it is worth
seeing the syntax now so it is not a surprise. To write through a
pointer to a struct field, Rexy uses a dedicated form:

```rust
unsafe {
    (*p).x = 10;
}
```

The parentheses and the leading `*` make the indirection explicit.
Chapter 9 introduces pointers properly, and Chapter 10 explains why
this syntax has to live inside `unsafe`.

### Public Structs

A struct declared inside a module is private to that module by default.
To let other modules construct it and read its fields, you mark it
`pub`:

```rust
pub struct Point {
    x: i32,
    y: i32,
}
```

You will see modules in Chapter 11. For now, just know the keyword
exists; programs in this chapter all live in a single file, so
visibility does not matter yet.

### Where You Are by the End of Chapter 6

You can declare a struct, build values of that struct, read its fields,
and pass it to and from functions. You have a clean way to give names
to grouped data.

You know:

- `struct Name { field: Type, ... }` declares a record type.
- `Name { field: value, ... }` builds a value of that type.
- `value.field` reads a field.
- Structs pass as arguments and return as values like any other type.
- `pub struct` exposes a struct across module boundaries.

What structs cannot do is express choice. A `Point` is always an `x`
and a `y`. There is no way to say "this is *either* a point *or* a
label". For that you need enums. Chapter 7 adds them.
