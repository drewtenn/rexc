# Part I - From Source Text to Syntax Tree

When Rexc begins, there is no program in the compiler's hands yet. There is
only text: bytes from a `.rx` file, arranged by a human into names, keywords,
punctuation, and literals. The first job is not to decide whether the program is
correct. The first job is to turn that text into a shape the rest of the
compiler can reason about.

Part I follows that transformation. Chapter 1 starts with the raw source file
and explains how Rexc breaks it into tokens. A token is a classified piece of
source text, such as `fn`, `main`, `i32`, `{`, or `return`. Chapter 2 takes
those tokens and builds an **AST** (Abstract Syntax Tree, the tree-shaped
representation of the program's grammar). Once the AST exists, the compiler no
longer has to remember where every character appeared. It can talk about
functions, statements, expressions, and types.

By the end of Part I, Rexc has not proved the program is meaningful yet. It has
proved something more basic: the source has the grammar of a Rexc program. That
is enough to hand the tree to the semantic analyzer, which is where the
compiler starts asking what the program means.

