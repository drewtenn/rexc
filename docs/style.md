# Documentation Style

This book is a tutorial for programmers who already know at least one
language and want to learn Rexy. It teaches by example. Every chapter
gives the reader code to type, a way to run it, and the output they
should see when they run it.

1. **Talk to the reader directly.** Use "you" voice. The book is a
   walkthrough: "open `hello.rx`", "type this in", "run it and you should
   see...". Do not use "we" or the passive voice.

2. **Pair every example with its output.** When a chapter introduces a
   construct, it shows code and shows what running that code produces. A
   `text` block immediately after the source captures the output the
   reader should see. Until the standard library tour in Chapter 13, the
   output is usually the program's exit status reported by `echo $?`.

3. **Build up one feature at a time.** Each section adds one new piece of
   syntax or one new rule. The reader edits a small program, runs it,
   sees the change, and keeps going. Long unbroken explanations are a
   sign that the section should be split.

4. **Show errors on purpose.** When a rule matters, show the reader what
   happens when they break it. Do not just say "the compiler will reject
   this". Give the program that fails, name the error, and let the
   reader see the diagnostic for themselves.

5. **Introduce language terms on first use.** Define each language term
   in plain English the first time it appears: binding, mutability,
   expression, statement, type annotation, primitive, slice, struct,
   enum variant, generic parameter, module, pointer, and the `unsafe`
   boundary.

6. **Order chapters so each construct rests on earlier ones.** A reader
   should be able to read front to back without meeting a construct
   before it has been introduced. Values and types come before bindings.
   Bindings come before functions. Functions come before control flow.
   Structs and enums come before pattern matching across them. Pointers
   come before `unsafe`. Modules come before the standard library tour.

7. **Keep the build command in the background.** Chapter 1 shows the
   full `rexc` invocation once. After that, prefer phrases like "rebuild
   and run" so the chapters can focus on Rexy itself. The reader copies
   the command from Chapter 1 when they need it.

8. **Use visuals when shape matters.** Memory layouts, struct fields,
   slice headers, stack frames, and module trees all have spatial
   shape. When diagrams are added, keep them generated, numbered by
   chapter, and referenced where the prose first needs them. Plain
   row-and-column facts should stay as native Markdown tables.

9. **Assume programming knowledge, not Rexy knowledge.** The reader
   understands functions, types, loops, and pointers in some other
   language. Do not spend time explaining what a function is. Do explain
   why Rexy requires type annotations on every binding, why mutation is
   opt-in, and why `unsafe` exists.

10. **End every chapter with what you can now do.** Each chapter ends
    with "Where You Are by the End of Chapter N", a short bullet list
    of what the reader can now write and a one-line preview of the next
    chapter.
