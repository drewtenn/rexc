# Documentation Style

This book is written for software engineers who already know at least one
programming language and want to learn Rexy. It follows the same teaching style
as the Drunix book: layered, conversational, concrete, and careful about what
the reader can already know.

1. **Narrate the language as the reader meets it.** Describe what a Rexy
   programmer can do at the exact point in the book the chapter covers. A
   chapter should not read like a feature list. Prefer "by the time we finish
   this chapter, we can write..." over "this chapter adds...".

2. **Introduce language terms on first use.** If a chapter uses a language
   term, define it in plain English the first time it appears. This includes
   binding, mutability, expression, statement, type annotation, primitive,
   slice, struct, enum variant, generic parameter, module, pointer, and the
   `unsafe` boundary.

3. **Show the syntax.** Code blocks are the medium of instruction in a language
   book. Pair every new construct with a small, self-contained Rexy example.
   Keep examples short enough that the surrounding prose can call out the
   pieces that matter.

4. **Use visuals when shape matters.** Memory layouts, struct fields, slice
   headers, stack frames, and module trees all have spatial shape. When
   diagrams are added, keep them generated, numbered by chapter, and referenced
   at the point where the prose first needs them. Plain row-and-column facts
   should stay as native Markdown tables.

5. **Order chapters so each construct rests on earlier ones.** A reader should
   be able to read front to back without meeting a construct before it has been
   introduced. Values and types come before bindings. Bindings come before
   functions. Functions come before control flow. Structs and enums come before
   pattern matching across them. Pointers come before `unsafe`. Modules come
   before the standard library tour.

6. **Use the warm "we" voice.** Write like a colleague pairing with the reader
   on their first Rexy program. Bring the reader along: "we now have a way to
   describe state", "we can finally express choice between two values", "we are
   ready to build something that runs on Drunix".

7. **Teach the language, not the toolchain.** Build commands matter, but they
   are not the subject. The subject is how to think and write in Rexy. Push
   compiler invocation, linker layout, and Drunix integration to the final
   chapter, where they belong as the bridge from "I wrote a Rexy program" to "I
   ran it on the operating system".

8. **Assume programming knowledge, not Rexy knowledge.** The reader understands
   functions, types, loops, and pointers in some other language. Do not spend
   time explaining what a function is. Do explain why Rexy requires type
   annotations on every binding, why mutation is opt-in, and why `unsafe`
   exists.

9. **End every chapter with what the reader can now write.** Each chapter ends
   with "Where We Are by the End of Chapter N". That section should say,
   plainly, what kind of Rexy program the reader is now equipped to write, and
   what the next chapter is ready to add.
