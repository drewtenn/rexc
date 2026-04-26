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

The IR keeps the facts the backend needs. Functions have full module-aware
names, parameters, return types, and statement bodies. Static buffers and
static scalars become explicit IR globals. Values carry resolved primitive
types. Integer literals keep their decimal text. Calls carry the resolved
callee name and the lowered argument values. Assignments carry a target local
or static scalar name and a typed value to store. Indirect assignments carry a
lowered pointer target and a typed value to write through it. Casts carry the
lowered source value and the resolved target type. Branches and loops carry
typed conditions and lowered statement bodies.

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

### Control Flow Becomes Backend-Friendly

An `if/else` node in the AST is still a source-level statement. In the IR it
becomes a branch statement with three direct pieces: the condition value, the
then body, and the else body. The condition's type is `bool`, not just "whatever
expression the source wrote." That is the contract code generation relies on
when it later emits a compare-against-zero and a conditional jump.

`while` lowers in the same spirit. The IR loop node carries a boolean condition
value and a lowered body. The backend does not need to rediscover that the
condition is legal or that the body is scoped; semantic analysis already proved
those facts, and lowering preserved them.

`break` and `continue` lower to marker statements. They do not carry their
target labels in IR because labels are a backend detail. Semantic analysis has
already proved they are inside a loop, so code generation can resolve them to
the innermost loop it is currently emitting.

The IR also keeps branch-local statements contained in their branch bodies.
Loop-local statements stay contained in loop bodies for the same reason. That
matches semantic analysis and makes stack-slot assignment possible later: each
`let` statement can be assigned its own storage even if another branch or loop
body uses the same source name.

### Mutation Targets Existing Storage

The IR does not need a separate "mutable local" flag. Mutability is a source
language rule, and semantic analysis has already enforced it. By the time
lowering creates an assignment statement, the target is known to be a visible
mutable local and the assigned value is known to have the target's type. When
lowering creates an indirect assignment, semantic analysis has already proved
that the target expression has pointer type and the value matches the pointee
type.

That leaves the backend with a small instruction-level job: evaluate the right
side and store the accumulator back into the existing stack slot for that local.
Declarations allocate slots; assignments reuse them. Indirect assignments use
the same idea with an extra address step: evaluate the value, evaluate the
pointer target, then store the value into the memory address held by that
pointer.

Static storage is explicit in IR because it does not live in a stack frame.
Static buffers lower to global storage declarations. Static scalars lower to
global scalar declarations plus load and store operations when expressions read
or assign them. This lets code generation put stack locals, mutable statics,
and static byte buffers in the right assembly sections without re-reading the
source AST.

Pointer indexing does not need its own IR node. The parser turns `p[i]` into
the same shape the programmer could have written directly: `*(p + i)`. That
means semantic analysis, lowering, and code generation all reuse the existing
unary dereference and binary pointer-addition paths.

### Where the Compiler Is by the End of Chapter 4

Rexc now holds a typed IR module. The source has been parsed, checked, and
lowered into a backend-facing representation. Every function and global has a
module-aware name. Every value has a primitive type. Every branch and loop
condition is already known to be boolean. Every explicit cast has a validated
source and target type. Every assignment targets an existing local or static
scalar with a value of the right type. Every indirect assignment has a pointer
target and a value of the pointee type. Every `break` and `continue` statement
is known to be inside a loop. Every function body is a sequence of typed IR
statements.

The compiler is ready to leave source-language territory. The next stage must
take this typed IR and make it concrete for a CPU: stack frames, registers,
instructions, labels, calls, and returns.
