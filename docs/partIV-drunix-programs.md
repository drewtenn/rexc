# Part IV - Linking Rexc Programs into Drunix

Assembly is close to the machine, but it is not yet a runnable program. The
assembler must turn it into an object file, and the linker must combine that
object with the startup code and runtime library that Drunix expects every user
program to have.

Part IV follows that final handoff. Chapter 6 explains how Rexc output fits
into the Drunix userland build path: source becomes assembly, assembly becomes
an ELF object, and the object links with `crt0.o`, `libc.a`, and the user
linker script to produce a 32-bit Drunix ELF executable.

By the end of Part IV, the compiler's output has crossed the boundary from
language artifact to operating-system artifact. Drunix does not need to know
that Rexc source produced the binary. It only needs a valid executable with the
right entry point and runtime contract.

