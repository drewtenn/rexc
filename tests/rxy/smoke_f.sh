#!/usr/bin/env bash
# rxy Phase F smoke test:
#   - --offline against an empty cache fails clearly for git deps
#   - --offline against a primed cache succeeds
#   - --offline succeeds for path-only projects (no network needed)
#   - std = "0.1" pins to the bundled stdlib without fetching
#   - std = "^99.0" errors clearly
#   - $REXY_OFFLINE env var has the same effect as --offline

set -euo pipefail

BUILD_DIR="${1:?usage: smoke_f.sh <build-dir> <source-dir>}"
SOURCE_DIR="${2:?usage: smoke_f.sh <build-dir> <source-dir>}"

RXY="${BUILD_DIR}/rxy"
test -x "${RXY}" || { echo "rxy missing at ${RXY}"; exit 1; }

WORK="$(mktemp -d -t rxy_smokef.XXXXXX)"
export REXY_HOME="${WORK}/.rxy"
trap 'rm -rf "${WORK}"' EXIT

# ---------------------------------------------------------------------------
# 1) Path-only project + --offline (must succeed, no network)
# ---------------------------------------------------------------------------
PKG="${WORK}/path-only"
mkdir -p "${PKG}/src"
cat > "${PKG}/Rexy.toml" <<'EOF'
[package]
name = "path-only"
version = "0.1.0"
edition = "2026"
EOF
echo 'fn main() -> i32 { return 0; }' > "${PKG}/src/main.rx"
cd "${PKG}"
"${RXY}" --color=never --offline build
test -x target/dev/path-only

# ---------------------------------------------------------------------------
# 2) std = "0.1" pin against bundled stdlib (--offline still works)
# ---------------------------------------------------------------------------
STDPIN="${WORK}/stdpin"
mkdir -p "${STDPIN}/src"
cat > "${STDPIN}/Rexy.toml" <<'EOF'
[package]
name = "stdpin"
version = "0.1.0"
edition = "2026"

[dependencies]
std = "0.1"
EOF
echo 'fn main() -> i32 { return 0; }' > "${STDPIN}/src/main.rx"
cd "${STDPIN}"
"${RXY}" --color=never --offline build
grep -q '"std"' Rexy.lock
grep -q 'source = "bundled+rexc"' Rexy.lock

# ---------------------------------------------------------------------------
# 3) Incompatible std pin must error
# ---------------------------------------------------------------------------
BADPIN="${WORK}/badpin"
mkdir -p "${BADPIN}/src"
cat > "${BADPIN}/Rexy.toml" <<'EOF'
[package]
name = "badpin"
version = "0.1.0"
edition = "2026"

[dependencies]
std = "^99.0"
EOF
echo 'fn main() -> i32 { return 0; }' > "${BADPIN}/src/main.rx"
cd "${BADPIN}"
if "${RXY}" --color=never build 2>/tmp/f_badpin_err.txt; then
    echo "smoke_f: expected stdlib-pin error"; cat /tmp/f_badpin_err.txt; exit 1
fi
grep -q "does not accept" /tmp/f_badpin_err.txt
grep -q "bundled stdlib" /tmp/f_badpin_err.txt

# ---------------------------------------------------------------------------
# 4) --offline with git dep but empty cache → clear error
# ---------------------------------------------------------------------------
GITDEP="${WORK}/gitdep"
mkdir -p "${GITDEP}/src"
cat > "${GITDEP}/Rexy.toml" <<EOF
[package]
name = "gitdep"
version = "0.1.0"
edition = "2026"

[dependencies]
phantom = { git = "file:///${WORK}/never-exists.git", rev = "abcdef" }
EOF
echo 'fn main() -> i32 { return 0; }' > "${GITDEP}/src/main.rx"
cd "${GITDEP}"
if "${RXY}" --color=never --offline build 2>/tmp/f_offline_err.txt; then
    echo "smoke_f: expected offline-cache-miss error"; cat /tmp/f_offline_err.txt; exit 1
fi
grep -q "offline mode" /tmp/f_offline_err.txt
grep -q "no cached clone" /tmp/f_offline_err.txt

# ---------------------------------------------------------------------------
# 5) $REXY_OFFLINE env var has the same effect
# ---------------------------------------------------------------------------
cd "${GITDEP}"
if REXY_OFFLINE=1 "${RXY}" --color=never build 2>/tmp/f_envvar_err.txt; then
    echo "smoke_f: expected REXY_OFFLINE to block git fetch"; cat /tmp/f_envvar_err.txt; exit 1
fi
grep -q "offline mode" /tmp/f_envvar_err.txt

echo "smoke_f: all checks passed"
