# The Rexy Programming Language

*A Systems Language for Drunix*

Rexy is the language side of the Drunix experiment. Drunix asks what it takes
to build an operating system from the first boot instruction upward. Rexy asks
the companion question: once the operating system can run user programs, what
does a small, modern systems language for those programs look like, and how
does someone learn to write in it?

This book is for programmers who already know at least one language and want
to add Rexy to that list. It starts with values and bindings, builds up through
control flow, structs, enums, pointers, modules, and generics, and ends by
turning a Rexy program into a Drunix executable.

The goal is not to hide the systems-language nature of Rexy. Memory is
visible. Mutation is opt-in. Pointer access lives behind an `unsafe` boundary.
Types are written down. We will follow those choices on purpose, because they
are the choices that make Rexy a useful language for an operating system to
host.

**Drew Tennenbaum**
