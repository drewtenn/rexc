# VS Code Rexc Extension Design

## Goal

Create a lightweight Visual Studio Code extension that gives Rexc source files
(`.rx`) first-class syntax highlighting and basic editor behavior comparable to
the built-in experience for C++ and Rust files.

## Scope

The extension will live in `tools/vscode-rexc/` as a standalone VS Code
extension. It will not add compiler integration, diagnostics, formatting, or a
language server in this pass. Those features can be added later without changing
the language id or file association.

## Language Registration

The extension registers a language with:

- Language id: `rexc`
- Extensions: `.rx`
- Aliases: `Rex`, `Rexc`, `rexc`
- Configuration: `language-configuration.json`

The language configuration will support line comments, bracket matching,
auto-closing brackets, auto-closing quotes, surrounding pairs, and indentation
around braces.

## Syntax Highlighting

The TextMate grammar will be based on `grammar/Rexc.g4` and the current standard
library/examples. It will recognize:

- Line comments beginning with `//`
- String literals with Rexc escape sequences
- Character literals with Rexc escape sequences
- Integer literals
- Keywords: `fn`, `extern`, `static`, `let`, `mut`, `return`, `if`, `else`,
  `while`, `break`, `continue`, `as`
- Primitive types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`,
  `bool`, `char`, `str`
- Boolean constants: `true`, `false`
- Function declarations and calls
- Operators and punctuation used by Rexc expressions, pointers, casts, arrays,
  blocks, and return types

The grammar will use standard TextMate scopes so existing VS Code themes can
color Rexc source naturally.

## Package Contents

`tools/vscode-rexc/` will contain:

- `package.json` for extension metadata and VS Code contribution points
- `README.md` with local development and installation instructions
- `language-configuration.json` for editor behavior
- `syntaxes/rexc.tmLanguage.json` for syntax highlighting
- `.vscodeignore` to keep packaged extensions small

## Verification

Verification will focus on static validity:

- Confirm the extension files are valid JSON.
- Confirm the grammar references the registered `rexc` language id.
- If Node tooling is available, run `npx @vscode/vsce package --no-dependencies`
  from `tools/vscode-rexc/` to verify the extension can package as a VSIX.

Manual verification after installation:

- Open any `.rx` file from `examples/` or `src/stdlib/`.
- Confirm comments, strings, chars, numbers, keywords, primitive types, function
  names, function calls, operators, and punctuation receive theme highlighting.
- Confirm braces, brackets, parentheses, comments, and quotes behave as expected
  while editing.
