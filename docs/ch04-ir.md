\newpage

## Chapter 4 - Lowering to Typed IR

### Why the Compiler Needs Another Shape

After semantic analysis, Rexc has a checked AST. That AST is useful because it
looks like the source program. It is less ideal for code generation, because it
still carries parser-facing details: source type names, source statement
classes, and expression nodes arranged for grammar first.

Rexc lowers the AST into **IR** (Intermediate Representation, a compiler-owned
program shape between the frontend and backend). The IR is not source text and
it is not assembly. It is the compiler's working form: typed, smaller than the
AST, and direct enough for assembly emission.

### What the IR Keeps

The IR keeps the facts the backend needs. Functions have names, parameters,
return types, and statement bodies. Values carry resolved primitive types.
Integer literals keep their decimal text. Calls carry the callee name and the
lowered argument values. Branches carry a typed condition and lowered statement
bodies.

The main shift is that source type names disappear. Once semantic analysis has
proved that `i32` is a valid primitive type, later stages do not need to parse
the string `i32` again. They receive a primitive type value directly. That
means code generation can ask simple questions: is this type signed? is it
64-bit? is this target able to emit it?

### Expected Types Still Matter During Lowering

Lowering mirrors part of semantic analysis because literals need context. If a
function returns `u64` and the source says `return 1;`, the lowered integer
value should be a `u64` integer value, not a default `i32`. If a call passes
`1` into an `i8` parameter, the lowered argument should remember that it is
`i8`.

This does not mean lowering is re-checking the whole program. Semantic analysis
has already decided whether the program is valid. Lowering follows the same
expected-type paths so the IR preserves the resolved type decisions that sema
already made.

### Branches Become Backend-Friendly

An `if/else` node in the AST is still a source-level statement. In the IR it
becomes a branch statement with three direct pieces: the condition value, the
then body, and the else body. The condition's type is `bool`, not just "whatever
expression the source wrote." That is the contract code generation relies on
when it later emits a compare-against-zero and a conditional jump.

The IR also keeps branch-local statements contained in their branch bodies.
That matches semantic analysis and makes stack-slot assignment possible later:
each `let` statement can be assigned its own storage even if another branch
uses the same source name.

### Where the Compiler Is by the End of Chapter 4

Rexc now holds a typed IR module. The source has been parsed, checked, and
lowered into a backend-facing representation. Every value has a primitive type.
Every branch condition is already known to be boolean. Every function body is a
sequence of typed IR statements.

The compiler is ready to leave source-language territory. The next stage must
take this typed IR and make it concrete for a CPU: stack frames, registers,
instructions, labels, calls, and returns.

