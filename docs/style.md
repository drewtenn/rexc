# Documentation Style

This book is written for software engineers who have not built a compiler for
an operating system before. It follows the same teaching style as the Drunix
book: layered, conversational, concrete, and careful about what the reader can
already know.

1. **Narrate the compiler flow.** Describe what is true at the exact moment the
   chapter covers. A chapter should not read like a feature list. Prefer "by
   the time this stage hands off, the program has become..." over "this chapter
   adds...".

2. **Introduce terms on first use.** If a chapter uses a compiler term, define
   it in plain English the first time it appears. This includes lexer, parser,
   AST, semantic analysis, IR, ABI, ELF, relocation, stack frame, and calling
   convention.

3. **Use minimal code blocks.** Code blocks belong only where exact syntax is
   the lesson: a short Rexc example, a compact table, or a tiny assembly shape.
   Prefer prose for implementation details when prose teaches the idea faster
   than pasted source.

4. **Use visuals when shape matters.** Compiler pipelines, syntax trees, stack
   frames, call frames, object-file layout, and linker inputs all have a
   spatial or staged shape. When diagrams are added, keep them generated,
   numbered by chapter, and referenced at the point where the prose first needs
   them. Plain row-and-column facts should stay as native Markdown tables.

5. **Order chapters in layers.** A reader should be able to read front to back
   without meeting a mechanism before it has been explained. Source text comes
   before tokens. Tokens come before syntax trees. Syntax trees come before
   type checking. Type checking comes before IR. IR comes before assembly.
   Assembly comes before object files and final linking.

6. **Use the warm "we" voice.** Write like a colleague walking through a system
   they genuinely enjoy. Bring the reader along: "we have a stream of tokens",
   "we can now reject this program", "we are ready to emit a stack frame".

7. **Teach implementation concepts, not command catalogues.** Commands matter,
   but they are not the subject. The subject is how source becomes a program
   Drunix can load. Explain the data structures, handoffs, contracts, and
   tradeoffs. Put command snippets where they prove the path, not where they
   replace the explanation.

8. **Assume software engineering knowledge, not compiler knowledge.** The
   reader understands data structures, functions, memory, and build systems.
   Do not spend time explaining what a vector is. Do explain why a parser needs
   precedence, why an IR is useful, and why a calling convention matters.

9. **End every chapter with current state.** Each chapter ends with
   "Where the Compiler Is by the End of Chapter N". That section should say,
   plainly, what the compiler can now accept, what representation exists, and
   what the next chapter is ready to consume.

