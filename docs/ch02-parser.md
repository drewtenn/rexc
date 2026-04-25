\newpage

## Chapter 2 - Parsing Functions and Statements

### From a Token Stream to a Program Shape

At the end of Chapter 1, Rexc had a token stream. That stream was cleaner than
raw text, but it was still flat. The compiler knew that `fn` was a keyword and
that `main` was an identifier, but it did not yet know that together they began
a function definition.

The **parser** performs that next step. A parser is the compiler stage that
checks whether tokens follow the language grammar and builds a structured
representation of that grammar. Rexc uses a hand-written recursive-descent
parser. Recursive descent means each grammar rule is represented by ordinary
C++ control flow: one function parses a function, another parses a block,
another parses an expression, and so on.

### Items, Blocks, and Statements

Rexc source is organised around functions. At the top level, the parser expects
either a function definition or an extern declaration. An extern declaration
names a function that exists outside the current Rexc source file, which lets
Rexc-generated code call into a runtime or library supplied by the final link.

Inside a function body, the parser builds statements. A statement is a piece of
program structure that does something in sequence. Rexc currently has seven
statement shapes:

| Statement | What it means |
| --- | --- |
| `let` | create an immutable local value with an explicit type |
| `let mut` | create a mutable local value with an explicit type |
| assignment | update an existing mutable local |
| `return` | produce the function result |
| `if` | conditionally run one block |
| `if/else` | choose between two blocks |
| `while` | repeat a block while a condition is true |

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
| Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` |
| Additive | `+`, `-` |
| Multiplicative | `*`, `/` |
| Unary | unary `-` |
| Primary | literals, names, calls, parenthesised expressions |

This layered parser gives each operator family a clear place. A comparison can
contain arithmetic on either side. Arithmetic can contain calls and names. A
parenthesised expression can override the default grouping when the source
needs a different shape.

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

Rexc can now turn a valid token stream into a syntax tree. It understands
function definitions, extern declarations, typed parameters, immutable and
mutable typed locals, assignments, returns, conditionals, while loops, calls,
literals, unary expressions, binary arithmetic, and comparisons.

The compiler still has not proved the program is meaningful. The tree might
refer to an unknown function. A return statement might produce the wrong type.
An assignment might target an immutable local. An `if` or `while` condition
might be an integer instead of a boolean. Those are semantic questions, and the
next stage is where Rexc begins to answer them.
