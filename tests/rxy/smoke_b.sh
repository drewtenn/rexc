#!/usr/bin/env bash
# rxy Phase B smoke test:
#   - app + util via path dep compiles and runs
#   - app + util via git dep (file:// repo) compiles and runs
#   - lockfile records git commit + checksum
#   - --locked succeeds when nothing changed; fails when source changes
#   - drift detection fires when checksum differs

set -euo pipefail

BUILD_DIR="${1:?usage: smoke_b.sh <build-dir> <source-dir>}"
SOURCE_DIR="${2:?usage: smoke_b.sh <build-dir> <source-dir>}"

RXY="${BUILD_DIR}/rxy"
test -x "${RXY}" || { echo "rxy missing at ${RXY}"; exit 1; }

WORK="$(mktemp -d -t rxy_smokeb.XXXXXX)"
export REXY_HOME="${WORK}/.rxy"
trap 'rm -rf "${WORK}"' EXIT

# ----- Build a `util` library package -----
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

# ----- App that consumes util via PATH dep -----
APP="${WORK}/app"
mkdir -p "${APP}/src"
cat > "${APP}/Rexy.toml" <<EOF
[package]
name = "app"
version = "0.1.0"
edition = "2026"

[dependencies]
util = { path = "../util" }
EOF
cat > "${APP}/src/main.rx" <<'EOF'
mod util;
use util::add;

fn main() -> i32 {
    return add(2, 3) - 5;
}
EOF

cd "${APP}"
"${RXY}" --color=never build
test -x target/dev/app
"${RXY}" --color=never run

# Lockfile should mention both packages
grep -q '"app"'  Rexy.lock
grep -q '"util"' Rexy.lock
grep -q 'source = "path+' Rexy.lock

# Second build with --locked must succeed
"${RXY}" --color=never build --locked >/dev/null

# Path dep edited → --locked still passes (path deps have no commit/checksum
# in v1, so any source changes just feed straight through).
echo '// trailing comment' >> "${UTIL}/src/util.rx"
"${RXY}" --color=never build --locked >/dev/null

# ----- Now exercise GIT dep against a local file:// repo -----
GITSRC="${WORK}/util-git"
cp -R "${UTIL}" "${GITSRC}"
cd "${GITSRC}"
git init -q -b main
git -c user.email=t@t -c user.name=t add . >/dev/null
git -c user.email=t@t -c user.name=t commit -q -m "v0.4.0"
git tag v0.4.0

APP2="${WORK}/app2"
mkdir -p "${APP2}/src"
cat > "${APP2}/Rexy.toml" <<EOF
[package]
name = "app2"
version = "0.1.0"
edition = "2026"

[dependencies]
util = { git = "file://${GITSRC}", tag = "v0.4.0" }
EOF
cat > "${APP2}/src/main.rx" <<'EOF'
mod util;
use util::add;

fn main() -> i32 {
    return add(10, -10);
}
EOF
cd "${APP2}"
"${RXY}" --color=never build
"${RXY}" --color=never run

# Lockfile must record commit + checksum
grep -q 'commit = "' Rexy.lock
grep -q 'checksum = "sha256:' Rexy.lock

# --locked passes when nothing changed
"${RXY}" --color=never build --locked >/dev/null

# Force-push the tag with different content → drift detection must fire
cd "${GITSRC}"
echo 'pub fn extra() -> i32 { return 1; }' >> src/util.rx
git -c user.email=t@t -c user.name=t commit -q -am "stealth update"
git tag -f v0.4.0 >/dev/null

cd "${APP2}"
if "${RXY}" --color=never build 2>/tmp/drift_err; then
    echo "smoke_b: expected drift error but build succeeded"
    cat /tmp/drift_err
    exit 1
fi
grep -q 'force-pushed\|now resolves to commit\|now hashes to' /tmp/drift_err || {
    echo "smoke_b: drift error message missing expected text"
    cat /tmp/drift_err
    exit 1
}

echo "smoke_b: all checks passed"
