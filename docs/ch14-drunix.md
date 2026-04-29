\newpage

## Chapter 14 - Building Programs for Drunix

### From Source to Something the OS Can Run

Throughout the book we have written Rexy code without dwelling on what
happens between saving a `.rx` file and seeing the program run. This
final chapter walks that path. The goal is not a comprehensive build
reference; the project's `README` covers every flag and target. The goal
is an end-to-end picture of how Rexy source becomes a Drunix executable.

### The Tools In Play

Three tools are involved between source and a runnable program:

- **`rexc`**, the Rexy compiler. It reads `.rx` files, type-checks them,
  and emits assembly, an object file, or a linked executable depending on
  what we asked for.
- **An assembler.** When `rexc` emits assembly, an assembler turns that
  text into an object file. `rexc` invokes one for us when we ask for
  object output or an executable.
- **A linker.** When the program needs to become a real executable, the
  linker combines our object with a startup object and the runtime
  library, using a linker script that describes the Drunix executable
  layout.

For everyday work we hand `rexc` a source file and an output path, and
`rexc` orchestrates the rest.

### The Smallest Useful Build

For a single-file program, the basic invocation produces a 32-bit Drunix
ELF executable:

```sh
rexc hello.rx --target i386-drunix --drunix-root "$DRUNIX" -o hello.drunix
```

The flags are doing three things. `--target i386-drunix` selects the
target triple: 32-bit Intel architecture, with the Drunix variant of the
runtime and ABI. `--drunix-root` points at a Drunix checkout that
contains the user-space linker script and the small startup object every
Drunix executable needs. `-o` names the output file.

Behind that single command, `rexc` walks through the same pipeline a
multi-tool build would walk through by hand: it produces an assembly
representation of the program, assembles it into an object, emits a
small Drunix startup object that provides `_start` and prepares `argc`,
`argv`, and `envp`, includes the target-specific hosted runtime, and
invokes the linker with the Drunix user linker script. The result is a
runnable executable.

### What `_start` and `main` Do Together

Drunix expects every user program to begin at a symbol called `_start`.
`_start` is provided by the startup object `rexc` emits as part of the
build. Its job is to set up the environment the program expects, call
the user's `main`, and then exit cleanly through Drunix's syscall
adapter.

Our Rexy `main` function returns an `i32`. The startup code reads that
return value, hands it to the operating system as the program's exit
status, and terminates the process. The convention from Chapter 1 lines
up with this: returning `0` says success, and any other value says
something the operating system or the parent process can interpret.

### Selecting Other Targets

The same compiler binary can target other architectures. The 64-bit Linux
target produces ELF64 objects:

```sh
rexc wide.rx --target x86_64 -c -o wide64.o
```

The `-c` flag produces an object file rather than an executable. Linking
that object into a runnable program requires an `x86_64` startup, runtime,
and linker script that match the host's calling convention. The Drunix
user runtime is currently focused on `i386-drunix`, so the 64-bit object
form is most useful as part of a separate build process or as a way to
inspect the compiler's emitted code on a different target.

There is also a Darwin ARM64 target useful during development on Apple
silicon hosts. The flag is `--target arm64-macos`. The mechanics are the
same as above: the compiler emits target-appropriate assembly and links
against the matching runtime.

### Inspecting the Result

Because Rexy programs become standard ELF executables, the same tools we
use to inspect any ELF file work here. `file` confirms the binary class
and architecture:

```sh
file hello.drunix
```

`readelf -h` shows the executable header, including the entry point and
the architecture:

```sh
x86_64-elf-readelf -h hello.drunix
```

For a 32-bit Drunix executable the header reports `ELF32` and `Intel
80386`, with the entry point set to the address of `_start`. Running the
executable on Drunix produces the program's output, exits with the value
`main` returned, and rejoins the operating system's run queue like any
other process.

### Where We Are by the End of the Book

We have walked from "I have never written a Rexy file" to "I can compile
a multi-file Rexy program with structs, enums, generics, modules,
pointers, and stdlib calls into a runnable Drunix executable". Along the
way we have met every construct the language currently provides and seen
what each one is for.

The compiler's internal pipeline -- how `rexc` actually performs lexing,
parsing, semantic analysis, IR lowering, code generation, and linking --
is its own subject and not the subject of this book. The README and the
compiler source are the right places to follow that thread further.
What this book has tried to give us is the part that comes first: a
working Rexy programmer.

From here, the way to learn more is to write Rexy. Pick a small problem.
Write it as a Rexy program. Run it on Drunix. The language is small
enough that a few weeks of practice will let it disappear into the
problem, which is the point at which a programming language has done its
job.
