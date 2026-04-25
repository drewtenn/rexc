\newpage

## Chapter 5 - Emitting x86 Assembly

### The Backend Receives a Typed Program

Code generation begins with an important advantage: it does not have to guess.
The backend receives typed IR. It knows whether a value is signed or unsigned,
whether a comparison produces `bool`, whether a local is a string pointer or an
integer-sized scalar, and whether the selected target is i386 or x86_64.

The backend's job is to turn those facts into assembly. Assembly is a textual
form of machine instructions. Rexc emits GNU assembler syntax, which means the
next tool in the chain can be `as` or `x86_64-elf-as`.

### Stack Frames and Local Slots

Every non-extern function gets a stack frame. A stack frame is the block of
stack memory a function uses for its saved frame pointer, parameters, locals,
and temporary call state. Rexc keeps the current model deliberately simple:
locals live in fixed stack slots, and expression results flow through the
accumulator register.

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

### Branches Become Labels and Jumps

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

### Where the Compiler Is by the End of Chapter 5

Rexc can now emit assembly for typed functions, locals, returns, calls,
arithmetic, division, comparisons, strings, and `if/else` branches. The i386
target is the default path for the current Drunix user runtime. The x86_64
target emits 64-bit Linux-compatible assembly using the System V calling
convention.

The compiler still has not produced an executable by itself. Assembly is the
input to the assembler, and the assembler produces an object file. To become a
Drunix program, that object must be linked with the user runtime and the right
linker script.

