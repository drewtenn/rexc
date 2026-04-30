#!/usr/bin/env bash
# rxy workspace-root build dispatch smoke test (PRD Appendix D deferred item):
#   - `rxy build` at a workspace root with no [package] walks every member
#   - default fail-fast: stops on the first member that fails
#   - --keep-going: builds every member, returns non-zero if any failed
#   - --bin is rejected at a workspace root

set -euo pipefail

BUILD_DIR="${1:?usage: smoke_h.sh <build-dir> <source-dir>}"
SOURCE_DIR="${2:?usage: smoke_h.sh <build-dir> <source-dir>}"

RXY="${BUILD_DIR}/rxy"
test -x "${RXY}" || { echo "rxy missing at ${RXY}"; exit 1; }

WORK="$(mktemp -d -t rxy_smokeh.XXXXXX)"
export REXY_HOME="${WORK}/.rxy"
trap 'rm -rf "${WORK}"' EXIT

# Layout:
#   ws/
#     Rexy.toml               (workspace root, no [package])
#     crates/ok_a/Rexy.toml   (builds)
#     crates/ok_b/Rexy.toml   (builds)
#     crates/broken/Rexy.toml (intentional rexc compile error)
WS="${WORK}/ws"
mkdir -p "${WS}/crates/ok_a/src" "${WS}/crates/ok_b/src" "${WS}/crates/broken/src"

cat > "${WS}/Rexy.toml" <<'EOF'
[workspace]
resolver = "2"
members = ["crates/*"]
EOF

for name in ok_a ok_b; do
    cat > "${WS}/crates/${name}/Rexy.toml" <<EOF
[package]
name = "${name}"
version = "0.1.0"
edition = "2026"
EOF
    echo 'fn main() -> i32 { return 0; }' > "${WS}/crates/${name}/src/main.rx"
done

cat > "${WS}/crates/broken/Rexy.toml" <<'EOF'
[package]
name = "broken"
version = "0.1.0"
edition = "2026"
EOF
# Compile error: undeclared identifier on RHS of return.
echo 'fn main() -> i32 { return undefined_symbol; }' > "${WS}/crates/broken/src/main.rx"

cd "${WS}"

# 1) --bin at workspace root is rejected.
if "${RXY}" --color=never build --bin foo 2>/tmp/h_bin.txt; then
    echo "smoke_h: expected --bin at workspace root to fail"; exit 1
fi
grep -q -- "--bin is not allowed at a workspace root" /tmp/h_bin.txt || {
    echo "smoke_h: missing --bin rejection message"; cat /tmp/h_bin.txt; exit 1
}

# 2) Default fail-fast: members are sorted, broken comes before ok_*.
#    The build must fail without attempting all members.
if "${RXY}" --color=never build >/tmp/h_ff.txt 2>&1; then
    echo "smoke_h: expected fail-fast workspace build to fail"; cat /tmp/h_ff.txt; exit 1
fi
grep -q "Building member \`broken\`" /tmp/h_ff.txt || {
    echo "smoke_h: did not see broken member dispatch"; cat /tmp/h_ff.txt; exit 1
}

# 3) --keep-going: build all members, exit non-zero, but every ok_* must build.
if "${RXY}" --color=never build --keep-going >/tmp/h_kg.txt 2>&1; then
    echo "smoke_h: expected --keep-going to still exit non-zero (one member failed)"
    cat /tmp/h_kg.txt; exit 1
fi
for name in ok_a ok_b broken; do
    grep -q "Building member \`${name}\`" /tmp/h_kg.txt || {
        echo "smoke_h: --keep-going did not visit member ${name}"
        cat /tmp/h_kg.txt; exit 1
    }
done
grep -q "workspace build:.*2 ok, 1 failed" /tmp/h_kg.txt || {
    echo "smoke_h: --keep-going did not summarize 2 ok / 1 failed"
    cat /tmp/h_kg.txt; exit 1
}

# 4) Drop the broken member; --keep-going (and default) succeed.
rm -rf "${WS}/crates/broken"
"${RXY}" --color=never build --keep-going >/tmp/h_ok.txt 2>&1
grep -q "Building member \`ok_a\`" /tmp/h_ok.txt
grep -q "Building member \`ok_b\`" /tmp/h_ok.txt

echo "smoke_h: all checks passed"
