# REXC

*Building a Systems Language Compiler for Drunix*

Rexc is the compiler side of the Drunix experiment. Drunix asks what it takes
to build an operating system from the first boot instruction upward. Rexc asks
the companion question: once the operating system can run user programs, what
does it take to build our own language that can produce those programs?

This book follows that path from source text to assembly, from assembly to an
ELF object, and finally to a Drunix user executable linked against the runtime
that Drunix already owns.

The goal is not to hide the compiler behind theory. The goal is to keep the
machine visible. At every stage we will ask what the compiler knows, what it
has proved, what it has thrown away, and what promise it is making to the next
stage.

**Drew Tennenbaum**

