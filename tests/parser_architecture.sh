#!/usr/bin/env bash
# Verifies src/parse.cpp uses generated ANTLR classes instead of a local parser.
set -euo pipefail

repo_root="${1:?repo root required}"
parser="${repo_root}/src/parse.cpp"

if grep -Eq 'enum class TokenKind|class Lexer|class Parser' "${parser}"; then
	echo "src/parse.cpp must use the generated ANTLR lexer/parser, not a handwritten parser" >&2
	exit 1
fi

grep -q 'RexcLexer' "${parser}"
grep -q 'RexcParser' "${parser}"
