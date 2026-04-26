\newpage

## Chapter 2 - Parsing Functions and Statements

### From a Token Stream to a Program Shape

At the end of Chapter 1, Rexc had a token stream. That stream was cleaner than
raw text, but it was still flat. The compiler knew that `fn` was a keyword and
that `main` was an identifier, but it did not yet know that together they began
a function definition.

The **parser** performs that next step. A parser is the compiler stage that
checks whether tokens follow the language grammar and builds a structured
representation of that grammar. Rexc uses ANTLR to generate its lexer and
parser from `grammar/Rexc.g4`. That keeps the accepted syntax in one grammar
file, while `src/parse.cpp` stays focused on invoking ANTLR and converting the
generated parse tree into Rexc's own AST.

### Items, Blocks, and Statements

Rexc source is organised around top-level **items**. A function definition is
one item, but it is no longer the only shape the parser accepts. Top-level
source can contain:

| Item | What it records |
| --- | --- |
| `fn` | a Rexc function definition with a body |
| `extern fn` | a function supplied by the runtime or final link |
| `static` / `static mut` scalar | a global scalar with an integer initializer |
| `static mut [T; N]` | a global byte-style buffer |
| `mod name { ... }` | an inline module containing nested items |
| `mod name;` | a file-backed module declaration |
| `use path::to::item;` | an imported item name for later lookup |

File-backed modules are part of parsing now. When the command-line driver
parses an entry file, `mod math;` causes Rexc to look for `math.rx` or
`math/mod.rx` beside the entry file, and then under any `--package-path` roots.
The loaded module is parsed with the correct module path so its functions and
globals keep names such as `math_add`.

Inside a function body, the parser builds statements. A statement is a piece of
program structure that does something in sequence. Rexc currently has these
statement shapes:

| Statement | What it means |
| --- | --- |
| `let` | create an immutable local value with an explicit type |
| `let mut` | create a mutable local value with an explicit type |
| assignment | update an existing mutable local |
| indirect assignment | write through a pointer, as in `*p = 9;` |
| call statement | call a function for its side effect |
| `return` | produce the function result |
| `if` | conditionally run one block |
| `if/else` | choose between two blocks |
| `while` | repeat a block while a condition is true |
| `break` | leave the nearest loop |
| `continue` | jump to the next check of the nearest loop |

When the parser sees braces, it enters a block. A block is a sequence of
statements surrounded by `{` and `}`. The parser does not yet decide whether a
name is visible in that block or whether a return type is correct. It only
preserves the shape so semantic analysis can answer those questions with the
whole function in view.

### Expressions and Precedence

Expressions are the parts of the language that produce values. A literal is an
expression. A name is an expression. A function call is an expression. Binary
operators such as `+` and `<` are expressions too, but they introduce a small
problem: order.

In `1 + 2 * 3`, multiplication binds more tightly than addition. The parser
must produce a tree where `2 * 3` is grouped first, then added to `1`. That rule
is called **precedence**. Precedence is the parser's way of turning a flat
operator sequence into the tree the programmer meant.

Rexc's current expression layers are:

| Layer | Operators or forms |
| --- | --- |
| Logical or | `||` |
| Logical and | `&&` |
| Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| Additive | `+`, `-` |
| Multiplicative | `*`, `/` |
| Cast | `as` followed by a type |
| Unary | unary `-`, `!`, `&`, `*` |
| Postfix | pointer indexing with `[index]` |
| Primary | literals, names, qualified calls, parenthesised expressions |

These grammar layers give each operator family a clear place. A comparison can
contain arithmetic on either side. Arithmetic can contain calls and names. A
parenthesised expression can override the default grouping when the source
needs a different shape.

Function calls use qualified names, so both `add(1, 2)` and
`math::add(1, 2)` have the same call-expression shape. The parser records the
path segments. Semantic analysis later decides whether that path names a
visible function.

### The AST as the Parser's Handoff

The parser's output is an **AST** (Abstract Syntax Tree). "Abstract" means the
tree keeps the program structure but leaves behind details that no longer
matter. Whitespace is gone. Comments are gone. Parentheses that only guided
grouping are gone. What remains is the shape the compiler will analyze:
functions contain parameters and statements, statements contain expressions,
and expressions contain other expressions.

This handoff is intentionally not typed. A binary expression records its
operator and operands, but it does not yet know whether `+` is being applied to
integers or accidentally to booleans. A type name records the spelling from the
source, but it is not yet resolved into the compiler's primitive type model.
That restraint keeps the parser simple. It answers the grammar question and
then stops.

### Where the Compiler Is by the End of Chapter 2

Rexc can now turn valid source text into a syntax tree. It understands
function definitions, extern declarations, file-backed and inline modules,
`use` imports, public visibility markers, static scalars, static buffers, typed
parameters, pointer types, immutable and mutable typed locals, assignments,
indirect pointer assignments, returns, conditionals, while loops, calls,
literals, unary expressions, pointer indexing, binary arithmetic, comparisons,
boolean operators, explicit casts, `break`, and `continue`.

The compiler still has not proved the program is meaningful. The tree might
refer to an unknown function or module item. A `use` declaration might import
a private symbol. A return statement might produce the wrong type. An
assignment might target an immutable local. An `if` or `while` condition might
be an integer instead of a boolean. A `break` might appear outside a loop.
Those are semantic questions, and the next stage is where Rexc begins to answer
them.
