\newpage

## Chapter 14 - Building and Running Your Programs

### From Source To Something The OS Can Run

Throughout the book you have been running programs without dwelling
on what happens between saving a `.rx` file and seeing the output.
This final chapter walks that path. The goal is not a comprehensive
build reference; the project's `README` covers every flag and target.
The goal is for you to understand the shape of the toolchain you have
been using.

### The Tools Involved

Three tools sit between your source and a runnable program:

- **`rexc`**, the Rexy compiler. It reads `.rx` files, type-checks
  them, and emits assembly, an object file, or a linked executable
  depending on what you asked for.
- **An assembler.** When `rexc` emits assembly, an assembler turns
  that text into an object file. `rexc` invokes one for you when you
  ask for object output or an executable.
- **A linker.** When the program needs to become a real executable,
  the linker combines your object with the platform's startup code
  and runtime so the operating system knows how to launch it.

For everyday work you hand `rexc` a source file and an output path,
and `rexc` orchestrates the rest.

### The Smallest Useful Build

For a single-file program on macOS, the basic invocation produces a
Mach-O command-line executable. Save this as `hello.rx`:

```rust
fn main() -> i32 {
    println("hello, rexy");
    return 0;
}
```

Build it:

```sh
rexc hello.rx --target arm64-macos -o hello
```

The flags do two things. `--target arm64-macos` selects the target
triple: 64-bit Apple Silicon, with the macOS variant of the runtime
and ABI. `-o` names the output file.

Run it:

```sh
./hello
```

```text
hello, rexy
```

Behind that single command, `rexc` walked through the same pipeline a
multi-tool build would walk through by hand: it produced Darwin ARM64
assembly, assembled it with Apple `as -arch arm64` into a Mach-O
object, and then linked the object with `clang -arch arm64` and the
normal macOS runtime startup.

### What `_main` And Your `main` Do Together

Every macOS executable starts at a startup symbol provided by the
system runtime. That symbol does the platform-specific setup, calls
your `main`, and then exits cleanly through the operating system's
exit syscall once `main` returns.

Your Rexy `main` returns an `i32`. The startup code reads that
return value, hands it to the operating system as the program's exit
status, and terminates the process. The convention from Chapter 1
lines up with this: returning `0` says success, and any other value
says something the operating system or the parent process can
interpret.

### Inspecting The Result

Because Rexy programs become standard Mach-O executables on macOS, the
same tools you use for any binary work here. `file` confirms the
binary class and architecture:

```sh
file hello
```

```text
hello: Mach-O 64-bit executable arm64
```

`otool` (or `nm`) shows the symbols:

```sh
nm hello | head
```

The header reports the architecture, the entry point, and the runtime
linkage. Running the executable produces the program's output, exits
with the value `main` returned, and rejoins the shell like any other
process.

### Stopping Short Of A Full Link

`rexc` can stop earlier in the pipeline if you want to inspect the
intermediate output. `-S` produces assembly text without assembling
it:

```sh
rexc hello.rx --target arm64-macos -S -o hello.s
```

`-c` produces a Mach-O object file without linking it:

```sh
rexc hello.rx --target arm64-macos -c -o hello.o
```

These are the same forms a C compiler offers, and they are useful
when you want to read what the compiler generated, when you need an
object you intend to link by hand, or when you are bringing up a new
target.

### Other Targets

The same compiler binary can target other architectures. The 64-bit
Linux target produces ELF64 objects:

```sh
rexc wide.rx --target x86_64 -c -o wide64.o
```

The `-c` flag produces an object file. Linking that object into a
runnable program on Linux requires an `x86_64` startup, runtime, and
linker script that match the host's calling convention. There is also
a 32-bit Intel target, `i386`, that produces ELF32 objects.

The default target this book uses is `arm64-macos` because it is the
fastest path from a Rexy source file to a binary you can run on a
modern Apple Silicon Mac. If you are on a different platform, or if
you want to produce binaries for a different operating system, the
README is the right place to read about the flag set those targets
expect.

### Where You Are by the End of the Book

You have walked from "I have never written a Rexy file" to "I can
compile a multi-file Rexy program with structs, enums, generics,
modules, pointers, and stdlib calls into a runnable executable".
Along the way you have met every construct the language currently
provides and seen what each one is for.

The compiler's internal pipeline -- how `rexc` actually performs
lexing, parsing, semantic analysis, IR lowering, code generation, and
linking -- is its own subject and not the subject of this book. The
README and the compiler source are the right places to follow that
thread further. What this book has tried to give you is the part that
comes first: a working Rexy programmer.

From here, the way to learn more is to write Rexy. Pick a small
problem. Write it as a Rexy program. Run it. The language is small
enough that a few weeks of practice will let it disappear into the
problem, which is the point at which a programming language has done
its job.
