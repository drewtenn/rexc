# Part II - Giving the Program Meaning

The parser gives Rexc a tree, but a tree is only grammar. A grammatically valid
program can still be wrong. It can return a string from a function that promised
an integer. It can use a local before the local exists. It can compare values
whose types do not match.

Part II covers the point where Rexc starts proving facts about the program.
Chapter 3 performs semantic analysis: it resolves names, checks primitive
types, validates calls, and rejects programs that do not make sense. Chapter 4
then lowers the checked AST into a typed intermediate representation, or IR.
The IR is smaller than the AST and more convenient for code generation, but it
keeps the facts semantic analysis proved.

By the end of Part II, Rexc has moved from "this source has the right grammar"
to "this program has a coherent typed meaning." That is the contract the
backend needs before it can safely emit assembly.

