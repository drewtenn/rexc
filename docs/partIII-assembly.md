# Part III - From Typed IR to Assembly

Once Rexc has typed IR, it has done the frontend's work. The remaining question
is physical: how should this program run on a target CPU?

Part III follows the backend through the x86 family. Chapter 5 explains how
Rexc maps typed values to stack slots and registers, emits arithmetic and
comparisons, lowers calls into the target calling convention, stores
assignments back into local or static storage, and turns `if/else` branches,
`while` loops, `break`, and `continue` into labels and jumps. The same IR can
target i386, x86_64, or Darwin ARM64 because each backend owns the
target-specific choices; this chapter focuses on the x86 implementation.

By the end of Part III, Rexc can write GNU assembler syntax. That assembly is
not necessarily the final executable yet, but it is no longer an abstract
program. It is a linear instruction stream that an assembler can turn into an
object file, or that the CLI can pass onward to an executable link.
