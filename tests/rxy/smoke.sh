#!/usr/bin/env bash
# rxy Phase A smoke test:
#   - rxy new produces a buildable project skeleton
#   - rxy build invokes rexc via the sibling-of-rxy lookup
#   - rxy run executes the resulting binary
#   - rxy --version works
#   - the lockfile is written and is deterministic

set -euo pipefail

BUILD_DIR="${1:?usage: smoke.sh <build-dir> <source-dir>}"
SOURCE_DIR="${2:?usage: smoke.sh <build-dir> <source-dir>}"

RXY="${BUILD_DIR}/rxy"
REXC="${BUILD_DIR}/rexc"

if [[ ! -x "${RXY}" ]]; then
    echo "smoke: rxy binary not at ${RXY}" >&2
    exit 1
fi
if [[ ! -x "${REXC}" ]]; then
    echo "smoke: rexc binary not at ${REXC} — rxy depends on rexc being built" >&2
    exit 1
fi

WORK="$(mktemp -d -t rxy_smoke.XXXXXX)"
trap 'rm -rf "${WORK}"' EXIT
cd "${WORK}"

# 1) --version
"${RXY}" --version | grep -E "^rxy [0-9]+\.[0-9]+\.[0-9]+" >/dev/null

# 2) new produces a project layout
"${RXY}" new hello --bin
test -f hello/Rexy.toml
test -f hello/src/main.rx
test -f hello/.gitignore

# 3) build (sibling-of-rxy lookup means rxy finds rexc next to it)
cd hello
"${RXY}" --color=never build

# 4) lockfile exists and is deterministic
test -f Rexy.lock
LOCK_BEFORE="$(cat Rexy.lock)"
"${RXY}" --color=never build >/dev/null
LOCK_AFTER="$(cat Rexy.lock)"
if [[ "${LOCK_BEFORE}" != "${LOCK_AFTER}" ]]; then
    echo "smoke: Rexy.lock changed between successive builds" >&2
    diff <(echo "${LOCK_BEFORE}") <(echo "${LOCK_AFTER}") >&2 || true
    exit 1
fi

# 5) run executes the binary (returns 0 from Hello, World)
"${RXY}" --color=never run

# 6) build with --release produces a different output path
"${RXY}" --color=never build --release
test -x target/release/hello
test -x target/dev/hello

# 7) --manifest-path works from another directory
cd "${WORK}"
"${RXY}" --color=never --manifest-path hello/Rexy.toml build >/dev/null

# 8) failure path: missing manifest
mkdir empty
cd empty
if "${RXY}" --color=never build 2>/dev/null; then
    echo "smoke: rxy build should fail when no Rexy.toml is found" >&2
    exit 1
fi

# 9) explicit REXC env var override
cd "${WORK}/hello"
REXC="${REXC}" "${RXY}" --color=never build >/dev/null

echo "smoke: all checks passed"
