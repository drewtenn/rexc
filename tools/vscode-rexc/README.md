# Rexc VS Code Extension

This extension adds syntax highlighting and basic editor behavior for Rexc
source files with the `.rx` extension.

## Features

- Registers `.rx` files as Rexc source.
- Highlights Rexc comments, literals, declarations, keywords, primitive types,
  function definitions, function calls, operators, and punctuation.
- Configures line comments, bracket matching, quote pairing, surrounding pairs,
  and brace indentation.

## Development

Open this directory in VS Code and press `F5` to launch an Extension Development
Host, then open a `.rx` file from the Rexc repository.

To verify the extension files without launching VS Code:

```sh
node scripts/verify-extension.mjs
```

To package a VSIX locally:

```sh
npx --yes @vscode/vsce package --no-dependencies
```

Install the generated VSIX from VS Code with **Extensions: Install from VSIX...**.
