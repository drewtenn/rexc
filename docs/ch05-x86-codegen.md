\newpage

## Chapter 5 - Emitting x86 Assembly

### The Backend Receives a Typed Program

Code generation begins with an important advantage: it does not have to guess.
The backend receives typed IR. It knows whether a value is signed or unsigned,
whether a comparison produces `bool`, whether a local is a string pointer or an
integer-sized scalar, whether an assignment targets existing local storage or a
pointer address, and whether the selected target is i386 or x86_64.

The backend's job is to turn those facts into assembly. Assembly is a textual
form of machine instructions. Rexc emits GNU assembler syntax, which means the
next tool in the chain can be `as` or `x86_64-elf-as`.

### Stack Frames and Local Slots

Every non-extern function gets a stack frame. A stack frame is the block of
stack memory a function uses for its saved frame pointer, parameters, locals,
and temporary call state. Rexc keeps the current model deliberately simple:
locals live in fixed stack slots, and expression results flow through the
accumulator register.

A `let` statement reserves one of those local slots and stores the initializer
there. An assignment does not reserve a new slot. Rexc emits the right side into
the accumulator, then writes that value back to the slot already associated
with the target local. That distinction is why semantic analysis had to reject
assignments to immutable locals before code generation began.

On i386, Rexc uses 32-bit slots for the currently supported scalar values. On
x86_64, Rexc uses 64-bit slots and aligns the local frame to preserve the
calling convention's stack expectations. Parameters follow the target ABI. An
**ABI** (Application Binary Interface) is the binary contract that says where
arguments go, which registers matter, how the stack is aligned, and how a
function returns.

The two current targets differ most visibly at function calls:

| Target | First argument path | Extra argument path |
| --- | --- | --- |
| `i386` | stack | stack |
| `x86_64` | registers such as `%rdi` and `%rsi` | stack after register arguments |

That difference is why the IR does not try to encode call mechanics. Calls are
a backend decision.

### Arithmetic, Division, and Comparisons

Simple arithmetic follows a repeated pattern. Rexc emits the left operand into
the accumulator, saves it, emits the right operand, moves the right operand into
a scratch register, restores the left operand, and then emits the target
instruction. The instruction suffix changes by target: i386 uses forms such as
`addl`, while x86_64 uses forms such as `addq`.

Division is more careful. x86 division instructions use an implicit dividend
spread across the accumulator and remainder registers. Signed division requires
sign extension first. Unsigned division requires clearing the remainder
register first. Because semantic analysis preserved signedness, the backend can
choose the right instruction shape without inspecting source text.

Comparisons produce `bool`. Rexc emits a compare instruction, then a condition
code instruction that writes one byte: `1` if the condition is true, `0` if it
is false. The result is then zero-extended back into the accumulator-sized
register. Signed comparisons use signed condition codes; unsigned comparisons
use unsigned ones.

Explicit casts are emitted after evaluating the source value. Many casts are
already in the right accumulator-sized representation, but narrowing casts need
the backend to make the target width visible. Unsigned narrow casts use
zero-extension, while signed narrow casts use sign-extension. On x86_64, casts
to `i32` sign-extend `%eax` into `%rax`, and casts to `u32` zero-extend by
writing through `%eax`.

### Pointer Loads and Stores

Pointer operations are small but important because they are the first feature
that treats a value as an address. For `&x`, the backend emits a
load-effective-address instruction: `leal` on i386 or `leaq` on x86_64. That
puts the stack slot address for `x` into the accumulator instead of loading the
value stored in that slot.

For `*p`, Rexc first emits `p` so the accumulator holds an address, then emits
a load from memory at that address. The load instruction follows the pointee
type: byte and word values are sign- or zero-extended, `i32` sign-extends on
x86_64, `u32` zero-extends through `%eax`, and pointer-sized values use the
target's normal word move.

For `*p = value`, Rexc evaluates the value, saves it briefly, evaluates the
pointer target, restores the value into a scratch register, and stores through
the address in the accumulator. The store width comes from the pointee type.

### Boolean Operators and Short-Circuit Jumps

Unary `!` is a small boolean transformation: emit the operand, compare the low
byte against zero, set `%al` when the value was false, and zero-extend the
result back into the full accumulator register.

The binary boolean operators are more interesting because `&&` and `||`
short-circuit. For `&&`, Rexc emits the left operand and jumps directly to the
false label if it is zero. Only a true left operand lets execution reach the
right operand. For `||`, the shape is reversed: a true left operand jumps
directly to the true label. In both cases, the final labels materialize a
normal `0` or `1` result in the accumulator.

That means a Rexc expression such as `ready && expensive()` preserves the usual
runtime promise: `expensive` is called only when `ready` is true.

### Control Flow Becomes Labels and Jumps

An `if/else` statement arrives in IR as a boolean condition plus two statement
bodies. Assembly has no tree-shaped branch node, so the backend turns that
shape into labels and jumps.

First, Rexc emits the condition. Since conditions are `bool`, the accumulator
holds zero or one. The backend compares the low byte against zero. If it is
zero, execution jumps to the else label. If it is non-zero, execution falls
through into the then body. After the then body, Rexc emits an unconditional
jump over the else body to the end label.

That gives the CPU a concrete path through what used to be a tree:

| Runtime condition | Instruction path |
| --- | --- |
| condition is false | jump to else label |
| condition is true | run then body, then jump to end label |

The interesting part is that this is the same idea on both targets. The exact
register names and instruction suffixes differ, but the control-flow shape is
shared.

A `while` loop uses the same pieces with a different label layout. Rexc emits a
start label, emits the loop condition, and jumps to the end label when the
condition is false. If the condition is true, execution falls through into the
body. After the body, an unconditional jump returns to the start label so the
condition can be tested again.

While emitting a loop body, the backend remembers that loop's start and end
labels. A `continue` statement becomes an unconditional jump to the current
start label. A `break` statement becomes an unconditional jump to the current
end label. For nested loops, the most recently entered loop is at the top of
that label stack, so `break` and `continue` naturally target the innermost
loop.

That creates the usual loop shape:

| Runtime condition | Instruction path |
| --- | --- |
| condition is false | jump to loop end |
| condition is true | run body, then jump back to loop start |

### Where the Compiler Is by the End of Chapter 5

Rexc can now emit assembly for typed functions, locals, returns, calls,
arithmetic, division, comparisons, explicit casts, boolean operators, strings,
address-of, dereference, direct and indirect assignment, `if/else` branches,
and `while` loops with `break` and `continue`. The i386 target is the default
path for the current Drunix user runtime. The x86_64 target emits 64-bit
Linux-compatible assembly using the System V calling convention.

The compiler still has not produced an executable by itself. Assembly is the
input to the assembler, and the assembler produces an object file. To become a
Drunix program, that object must be linked with the user runtime and the right
linker script.
