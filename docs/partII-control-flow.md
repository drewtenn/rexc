# Part II - Control Flow

A straight-line program runs the same statements in the same order every
time. Real programs choose, repeat, and respond to data. Part II covers the
two ways Rexy expresses that.

Chapter 4 introduces the C-family side of Rexy's control flow: `if` and
`else` for choice, `while` and `for` for repetition, and `break` and
`continue` for loop control. These are the constructs we reach for when the
condition is a simple boolean test. Chapter 5 covers `match`, the structured
form of choice. `match` becomes essential the moment we have enums and
struct-shaped data, but it is a useful tool over plain integers and
characters from the start.

By the end of Part II, the reader can write programs that branch on
conditions, iterate over a counter, and dispatch on the value or shape of a
piece of data. Together with bindings and functions, that is enough to
express any computation we want to express, even before we have composite
types.
