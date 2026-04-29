# Part IV - Memory and the `unsafe` Boundary

Rexy is a systems language. That commitment becomes most concrete in
Part IV, where you deal directly with addresses, with mutable storage
that lives for the whole program, and with the rule that protects the
rest of the language from those two capabilities.

Chapter 9 introduces pointers and statics together. Pointers let you
refer to a value by its address, dereference it, and walk through a run
of values. Statics give you program-lifetime storage, both read-only
scalars and mutable buffers. Chapter 10 introduces `unsafe`, the
keyword that fences off the operations that need explicit programmer
attention.

By the end of Part IV, you will be able to describe and manipulate
memory the way the operating system itself eventually has to. That is
also where Rexy demands the most discipline. You will spend the chapter
seeing why, not just how.
