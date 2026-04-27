\newpage

## Chapter 6 - Building a Drunix ELF Program

### The Compiler Can Stop at Several Boundaries

`rexc` can stop at assembly output, stop after object-file assembly, or drive a
final executable link. Those are command-line choices rather than different
compiler pipelines. The compiler owns source analysis, typing, lowering, and
target assembly emission. The assembler and linker still own the binary
container that Drunix will eventually load.

The assembler turns textual assembly into an object file. An object file is a
binary file containing machine code, data, symbols, and relocation information.
Relocation information records places where the linker may need to adjust an
address once all input objects are laid out together.

Rexy's i386 path is the current Drunix userland path. The generated assembly
can be assembled as a 32-bit x86 object file directly:

```sh
build/rexc examples/core.rx --target i386 -S -o build/core.s
x86_64-elf-as --32 -o build/core.o build/core.s
```

or through Rexy's object-output mode:

```sh
build/rexc examples/core.rx --target i386 -c -o build/core.o
```

The command is short, but the state change is significant. Before assembly,
Rexy had text that described instructions. After assembly, the build has an
ELF object file containing real i386 machine code.

### Why the Runtime Is Part of the Program

A Rexy function named `main` is not the first instruction Drunix runs in a user
process. Drunix starts at the executable entry point. That entry point belongs
to the user runtime's startup object, commonly called `crt0.o`. The startup
object receives the process stack prepared by the kernel, extracts `argc`,
`argv`, and `envp`, calls `main`, and exits through the syscall path when
`main` returns.

That startup path is why Rexy output must be linked with the Drunix runtime.
The compiler can emit a perfectly good `main`, but it does not emit the whole
process contract. The runtime supplies the bridge between the kernel's process
entry state and the language-level function the program author wrote.

The runtime archive, `libc.a`, supplies the userland support code the program
may call. An archive is a collection of object files. The linker pulls in only
the archive members needed to satisfy unresolved symbols, which keeps the final
program from dragging in the whole library when it only needs a small part.

### The Linker Gives the Program Its Final Shape

The linker combines the startup object, the Rexy-generated object, Rexy's
hosted runtime object when standard-library calls need it, and the Drunix
runtime archive under the control of the Drunix user linker script. A linker
script is a set of layout instructions for the linker. It decides where code
and data live in the final address space and which symbol becomes the entry
point.

For the current i386 Drunix path, the link has this shape:

| Link input | Role |
| --- | --- |
| `crt0.o` | process entry and call into `main` |
| Rexy object | program code emitted from `.rx` source |
| Rexy hosted runtime object | target-specific hooks and compiled stdlib support |
| `libc.a` | runtime and library support |
| `user.ld` | final executable layout |

The order matters. The runtime archive should come after the program object so
the linker sees the program's unresolved symbols first and can pull the needed
archive members afterward.

`rexc` can drive that final link when given a Drunix root:

```sh
build/rexc examples/core.rx \
    --target i386-drunix \
    --drunix-root /path/to/DrunixOS \
    -o build/core.drunix
```

### ELF Is the Shared Contract

The final output is an **ELF** (Executable and Linkable Format) executable. ELF
is the binary container format Drunix follows for user programs. It gives the
kernel enough structure to find loadable segments, map them into a process, and
start execution at the recorded entry point.

That is the quiet payoff of the whole pipeline. Drunix does not need a special
"Rexy program" loader. Once the final link succeeds, the kernel sees an ELF
binary with the same kind of process contract as a C program. The source
language has disappeared from the operating system boundary.

The useful validation checks are simple: `file` should identify a 32-bit Intel
80386 executable, `readelf` should show an ELF32 executable for machine Intel
80386, and the first four bytes should be the ELF magic number.

### Where the Compiler Is by the End of Chapter 6

`rexc` can now participate in the Drunix userland story. A `.rx` source file can
be parsed, checked, lowered, emitted as i386 assembly, assembled into an object
file, and linked with Drunix's startup object, Rexy's hosted runtime support,
and the Drunix runtime archive into a final ELF executable.

The operating system boundary remains clean. Drunix loads an ELF program. Rexy
is responsible for producing code that fits the runtime contract. The next
chapters can expand the language itself, but the first complete path from
source text to Drunix user program is now visible end to end.
