#!/usr/bin/env bash
# rxy Phase C smoke test:
#   - rxy publish records a registry entry against a local FS registry
#   - rxy add registers a registry dep and rxy build resolves it
#   - rxy yank flips the entry; fresh resolution skips it
#   - rxy unyank reverses
#   - reserved-prefix names are rejected by publish

set -euo pipefail

BUILD_DIR="${1:?usage: smoke_c.sh <build-dir> <source-dir>}"
SOURCE_DIR="${2:?usage: smoke_c.sh <build-dir> <source-dir>}"

RXY="${BUILD_DIR}/rxy"
test -x "${RXY}" || { echo "rxy missing at ${RXY}"; exit 1; }

WORK="$(mktemp -d -t rxy_smokec.XXXXXX)"
export REXY_HOME="${WORK}/.rxy"
trap 'rm -rf "${WORK}"' EXIT

# ----- Build a publishable `util` package as a git repo -----
UTIL="${WORK}/util"
mkdir -p "${UTIL}/src"
cat > "${UTIL}/Rexy.toml" <<'EOF'
[package]
name = "util"
version = "0.4.0"
edition = "2026"

[targets.lib]
path = "src/util.rx"
EOF
cat > "${UTIL}/src/util.rx" <<'EOF'
pub fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
EOF
cd "${UTIL}"
git init -q -b main
git remote add origin "file://${UTIL}"
git -c user.email=t@t -c user.name=t add . >/dev/null
git -c user.email=t@t -c user.name=t commit -q -m "v0.4.0"

# ----- rxy publish (to the local-FS default registry) -----
"${RXY}" --color=never publish

# Verify the registry entry exists
test -f "${REXY_HOME}/registries/default/index/packages/ut/il/util.toml"
grep -q 'version = "0.4.0"' "${REXY_HOME}/registries/default/index/packages/ut/il/util.toml"

# Re-publishing the same version must fail
if "${RXY}" --color=never publish 2>/tmp/dup_err; then
    echo "smoke_c: expected duplicate-publish error"; cat /tmp/dup_err; exit 1
fi
grep -q "already published" /tmp/dup_err

# ----- App that uses util via the registry -----
APP="${WORK}/app"
mkdir -p "${APP}/src"
cat > "${APP}/Rexy.toml" <<'EOF'
[package]
name = "app"
version = "0.1.0"
edition = "2026"

[dependencies]
util = "0.4"
EOF
cat > "${APP}/src/main.rx" <<'EOF'
mod util;
use util::add;

fn main() -> i32 {
    return add(7, -7);
}
EOF
cd "${APP}"
"${RXY}" --color=never build
"${RXY}" --color=never run
grep -q 'source = "registry+default"' Rexy.lock

# ----- Yank with security severity → fresh resolution should skip -----
"${RXY}" --color=never yank util@0.4.0 --severity security --reason "CVE-test"
grep -q 'yanked = true' "${REXY_HOME}/registries/default/index/packages/ut/il/util.toml"

# Bump util to 0.4.1 and republish so there's a non-yanked candidate
cd "${UTIL}"
sed -i.bak 's/0.4.0/0.4.1/' Rexy.toml; rm -f Rexy.toml.bak
git -c user.email=t@t -c user.name=t commit -q -am "v0.4.1"
"${RXY}" --color=never publish

# A *fresh* clone of the app (without lockfile) should resolve to 0.4.1
APP2="${WORK}/app2"
cp -R "${APP}" "${APP2}"
rm "${APP2}/Rexy.lock"
cd "${APP2}"
"${RXY}" --color=never build
grep -q '"util 0.4.1"' Rexy.lock || { echo "smoke_c: expected util 0.4.1 in fresh resolution"; cat Rexy.lock; exit 1; }

# ----- unyank reverses -----
"${RXY}" --color=never unyank util@0.4.0
grep -q 'yanked = false' "${REXY_HOME}/registries/default/index/packages/ut/il/util.toml" || {
    echo "smoke_c: unyank did not flip yanked = false"; exit 1; }

# ----- Reserved prefix is refused -----
RESERVED="${WORK}/std-reserved"
cp -R "${UTIL}" "${RESERVED}"
sed -i.bak 's/name = "util"/name = "std-reserved"/' "${RESERVED}/Rexy.toml"
rm -f "${RESERVED}/Rexy.toml.bak"
cd "${RESERVED}"
git -c user.email=t@t -c user.name=t commit -q --allow-empty -m "ren"
if "${RXY}" --color=never publish 2>/tmp/res_err; then
    echo "smoke_c: expected reserved-prefix rejection"; exit 1
fi
grep -q "reserved prefix" /tmp/res_err

# ----- rxy add round-trip -----
APP3="${WORK}/app3"
mkdir -p "${APP3}/src"
cat > "${APP3}/Rexy.toml" <<'EOF'
[package]
name = "app3"
version = "0.1.0"
edition = "2026"
EOF
cat > "${APP3}/src/main.rx" <<'EOF'
mod util;
use util::add;
fn main() -> i32 { return add(0, 0); }
EOF
cd "${APP3}"
"${RXY}" --color=never add util@0.4
grep -q 'util = "0.4"' Rexy.toml
"${RXY}" --color=never build

# ----- rxy remove -----
"${RXY}" --color=never remove util
if grep -q 'util' Rexy.toml; then
    echo "smoke_c: rxy remove did not delete util"; cat Rexy.toml; exit 1
fi

echo "smoke_c: all checks passed"
