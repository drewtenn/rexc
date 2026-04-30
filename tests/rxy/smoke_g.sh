#!/usr/bin/env bash
# rxy Phase G smoke test:
#   - reserved subcommands (fmt, doc, bench) error cleanly with phase reference
#   - per-subcommand --help works
#   - rxy lockfile prints human + --json formats
#   - --version prints

set -euo pipefail

BUILD_DIR="${1:?usage: smoke_g.sh <build-dir> <source-dir>}"
SOURCE_DIR="${2:?usage: smoke_g.sh <build-dir> <source-dir>}"

RXY="${BUILD_DIR}/rxy"
test -x "${RXY}" || { echo "rxy missing at ${RXY}"; exit 1; }

WORK="$(mktemp -d -t rxy_smokeg.XXXXXX)"
export REXY_HOME="${WORK}/.rxy"
trap 'rm -rf "${WORK}"' EXIT

# 1) Reserved stubs error cleanly with phase reference
for cmd in fmt doc bench; do
    if "${RXY}" --color=never "${cmd}" 2>/tmp/g_stub.txt; then
        echo "smoke_g: expected `${cmd}` to error"; exit 1
    fi
    grep -q "reserved subcommand" /tmp/g_stub.txt
    grep -q "FR-039" /tmp/g_stub.txt
done

# 2) Per-subcommand --help works for at least 4 commands
for cmd in build run test publish add lockfile new yank; do
    "${RXY}" "${cmd}" --help >/tmp/g_help.txt
    grep -q "Usage: rxy ${cmd}" /tmp/g_help.txt || {
        echo "smoke_g: rxy ${cmd} --help missing 'Usage:' line"; cat /tmp/g_help.txt; exit 1
    }
done

# 3) lockfile inspection
PKG="${WORK}/g"
mkdir -p "${PKG}/src"
cat > "${PKG}/Rexy.toml" <<'EOF'
[package]
name = "g"
version = "0.1.0"
edition = "2026"
EOF
echo 'fn main() -> i32 { return 0; }' > "${PKG}/src/main.rx"
cd "${PKG}"
"${RXY}" --color=never build >/dev/null

# Human-readable form
"${RXY}" lockfile > /tmp/g_lock_human.txt
grep -q "g 0.1.0" /tmp/g_lock_human.txt
grep -q "source: local" /tmp/g_lock_human.txt

# JSON form is parseable single-object
"${RXY}" lockfile --json > /tmp/g_lock_json.txt
python3 -c 'import json,sys; d=json.load(open("/tmp/g_lock_json.txt"));
assert d["version"] == 1
assert d["packages"][0]["name"] == "g"
assert d["packages"][0]["version"] == "0.1.0"
assert d["packages"][0]["source"] == "local"
print("json-ok")
' | grep -q json-ok

# 4) --version
"${RXY}" --version | grep -E "^rxy [0-9]+\.[0-9]+\.[0-9]+" >/dev/null

echo "smoke_g: all checks passed"
