\newpage

## Chapter 6 - Structs

### Naming a Group of Values

A **struct** is a named record of fields. Each field has its own name and
its own type. Structs let us bundle related values into a single thing the
program can pass around, store, and reason about as one unit.

We declare a struct at the top level of a file, alongside functions:

```rust
struct Point {
    x: i32,
    y: i32,
}
```

The declaration creates a new type called `Point`. From this point on,
`Point` may appear anywhere a primitive type may appear: in a parameter
list, as a return type, or as the type of a `let` binding.

Field declarations are separated by commas. A trailing comma after the last
field is allowed. The compiler does not care about whether the fields are on
their own lines, but the convention in this book is one field per line for
anything beyond the smallest structs.

### Building a Struct Value

To build a value of a struct type, we use a **struct literal**:

```rust
let origin: Point = Point { x: 0, y: 0 };
```

The literal names the struct, then provides each field by name. Every field
the struct declares must appear, and a field name may not appear twice. The
order does not have to match the declaration order, but readers will thank
us if it usually does.

Struct values can be passed to functions like any other value:

```rust
fn shifted(p: Point, dx: i32) -> Point {
    return Point { x: p.x + dx, y: p.y };
}
```

The return type of `shifted` is `Point`. Inside the body, `p.x` and `p.y`
read the fields of the parameter, and the return statement constructs a
fresh `Point`.

### Reading Fields

Field access uses a dot:

```rust
let p: Point = Point { x: 3, y: 4 };
let x: i32 = p.x;
let y: i32 = p.y;
```

A field expression has the type of the field. The same dot syntax works
inside any larger expression. There is nothing magical about it: it is just
the way to ask a struct value for one of its parts.

### Mutating Fields Through a Pointer

Fields of a local struct can be re-bound by assigning a new struct value to
the local. To mutate a field of a struct that is referenced through a
pointer, Rexy provides a dedicated form. We will cover pointers in detail in
Chapter 9, but the syntax is worth seeing now:

```rust
unsafe {
    (*p).x = 10;
}
```

That is the form Rexy uses to write through a pointer to a struct field.
The parentheses and the leading `*` make the indirection explicit, which is
the same explicitness theme we have followed since Chapter 2: when we
change something, the code shows where and how.

### Public Structs

A struct declared inside a module is private to that module by default. To
let other modules construct it and read its fields, we mark it `pub`:

```rust
pub struct Point {
    x: i32,
    y: i32,
}
```

Visibility becomes interesting once we have more than one module. Chapter
11 covers modules and the visibility rules around them in full. For now it
is enough to know that the keyword exists and that we will use it where
needed.

### Where We Are by the End of Chapter 6

We can declare a struct, build values of that struct, read its fields, and
hand it to functions. We have a clean way to give names to grouped data.

What structs cannot do is express choice. A `Point` is always an `x` and a
`y`. There is no way to say "this is *either* a point *or* a label". For
that we need enums. Chapter 7 adds them, and once both are in our toolbox
we can model the data of almost any program we will write.
