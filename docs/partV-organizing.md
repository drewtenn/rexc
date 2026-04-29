# Part V - Organising Programs

A real program rarely fits in one file. Sometimes it is too big. More
often, some of its pieces are reusable, some are specific, and the
codebase wants to draw the line between them. Rexy gives you two tools
for that.

Chapter 11 introduces **modules**: how a Rexy file splits into named
namespaces, how visibility works, and how `use` declarations bring
names in. Chapter 12 introduces **generics**, which let you write a
function or a struct once and reuse it across types.

By the end of Part V, you will have the tools to grow a program past
one file and to write data structures and algorithms that work for more
than one specific type.
