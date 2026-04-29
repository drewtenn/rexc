# Part III - Building Data

The values you have seen so far are isolated: a number, a boolean, a
character. Every interesting program eventually wants to put values
together into something with shape. Part III covers the four ways Rexy
lets you do that.

Chapter 6 introduces **structs**, named records of fields. Chapter 7
introduces **enums**, tagged unions whose variants can carry data.
Chapter 8 covers two smaller composites: **tuples**, which group a fixed
number of typed values without naming the fields, and **slices**, which
give you a uniform way to refer to a contiguous run of values of the
same type.

By the end of Part III, you will be able to model the data your program
is about. Functions can take structs and enums as arguments, return
them as values, and `match` over their variants and fields.
