# Rexc VS Code Extension

This extension adds syntax highlighting and basic editor behavior for Rexc
source files with the `.rx` extension.

## Features

- Registers `.rx` files as Rexc source.
- Highlights Rexc comments, literals, declarations, keywords, primitive types,
  function definitions, function calls, operators, and punctuation.
- Configures line comments, bracket matching, quote pairing, surrounding pairs,
  and brace indentation.

## Build

The extension lives in `tools/vscode-rexc` and does not have a compile step.
Building it means validating the extension metadata and grammar files, then
packaging them into a VSIX.

Prerequisites:

- Node.js with `npx`
- VS Code, if you want to launch or install the extension locally

From the repository root:

```sh
cd tools/vscode-rexc
node scripts/verify-extension.mjs
npx --yes @vscode/vsce package --no-dependencies
```

The packaging command writes a `rexc-<version>.vsix` file in this directory.
Install it from VS Code with **Extensions: Install from VSIX...**.

## Development

To run the extension without packaging it, open `tools/vscode-rexc` in VS Code
and press `F5` to launch an Extension Development Host, then open a `.rx` file
from the Rexc repository.
