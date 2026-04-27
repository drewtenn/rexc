\newpage

## Chapter 1 - Source Text and Tokens

### The Moment the Compiler Opens the File

Rexy begins in a quieter place than Drunix. There is no firmware handoff, no
CPU mode to claim, and no interrupt table waiting to be installed. The first
state is simply a source file loaded into memory. The command-line driver has
read the `.rx` file into a `SourceFile`, and from this point forward every
diagnostic can point back to a path, line, and column.

That location tracking matters immediately. A compiler spends a lot of its time
saying no, and a useful no has to name the exact part of the program that made
the compiler stop. Before Rexy knows whether a function exists or a type is
valid, it must know where every piece of text came from.

### Turning Characters into Tokens

The first compiler stage is the **lexer**. A lexer is the part of a compiler
that reads raw characters and groups them into tokens. A token is a small
classified unit of source text: an identifier, a keyword, a number, a string
literal, or punctuation such as `(` and `{`.

Consider this small Rexy program:

```rust
fn main() -> i32 {
    return 42;
}
```

The lexer does not see a function yet. It sees a sequence of characters. Its
job is to walk from left to right and produce a stream with more structure:

| Source text | Token role |
| --- | --- |
| `fn` | function keyword |
| `main` | identifier |
| `(` | left parenthesis |
| `)` | right parenthesis |
| `->` | return-type arrow |
| `i32` | identifier used as a type name |
| `{` | left brace |
| `return` | return keyword |
| `42` | integer literal |
| `;` | statement terminator |
| `}` | right brace |

This might look like a small improvement, but it is the first important
boundary in the compiler. Later stages do not need to ask whether the letter
`f` followed by the letter `n` is a keyword. They receive a token that already
answers that question.

### Whitespace, Comments, and Source Locations

Whitespace separates tokens, but it is not itself part of the program shape.
The lexer skips spaces, tabs, and newlines after using them indirectly through
the source-location table. Line comments are treated the same way: once the
lexer sees `//`, it advances to the end of the line and resumes from the next
character.

The important thing is that skipping text does not mean losing accountability.
Every token keeps the offset where it began. When a later stage reports an
unknown type or a mismatched operand, it can ask the original `SourceFile` to
translate that offset into a human-readable line and column. The compiler's
memory of the source remains precise even after the source has been transformed
into tokens.

### Literals Keep Their Original Text

Integer literals carry two pieces of information. They carry the parsed numeric
value where that is convenient, but they also keep the original decimal text.
That second detail is easy to miss, and it matters.

Some Rexy integers are larger than the host type the parser uses for quick
conversion. A `u64` literal such as `18446744073709551615` is a perfectly valid
source value, but it does not fit into a signed 64-bit parser result. If the
compiler threw away the original text too early, it could accidentally wrap the
value and type-check the wrong number. Rexy avoids that by letting semantic
analysis inspect the literal's decimal spelling later, when the expected type
is known.

Strings and characters take a different path. The lexer decodes escape
sequences such as newline and tab into the literal value the program meant.
That gives the parser a clean token while still letting diagnostics point back
to the source if an escape sequence is malformed or a literal is unterminated.

### Where the Compiler Is by the End of Chapter 1

`rexc` has now crossed its first boundary. The input is no longer anonymous text.
It is a stream of tokens, and every token knows where it came from.

The compiler can recognise keywords, identifiers, punctuation, operators,
integer literals, booleans, characters, strings, comments, and whitespace. It
can report lexical errors with source locations. It has not yet decided whether
the token stream forms a valid program. That is the parser's job, and it begins
with the question the lexer deliberately avoided: what larger structure do
these tokens describe?

