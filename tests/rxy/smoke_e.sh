#!/usr/bin/env bash
# rxy Phase E smoke test:
#   - 2-member workspace builds via path deps + workspace inheritance
#   - rxy build --target=<triple> places output under target/<triple>/<profile>/
#   - rxy test runs declared [[targets.test]] entries, exit-code-based pass/fail
#   - rxy test with cross-target reports skipped count
#   - profile knobs emit "recorded but not yet wired" warnings

set -euo pipefail

BUILD_DIR="${1:?usage: smoke_e.sh <build-dir> <source-dir>}"
SOURCE_DIR="${2:?usage: smoke_e.sh <build-dir> <source-dir>}"

RXY="${BUILD_DIR}/rxy"
test -x "${RXY}" || { echo "rxy missing at ${RXY}"; exit 1; }

WORK="$(mktemp -d -t rxy_smokee.XXXXXX)"
export REXY_HOME="${WORK}/.rxy"
trap 'rm -rf "${WORK}"' EXIT

# ---------------------------------------------------------------------------
# 1) Two-member workspace: util (lib) + app (bin) with inheritance
# ---------------------------------------------------------------------------
WS="${WORK}/ws"
mkdir -p "${WS}"
cat > "${WS}/Rexy.toml" <<'EOF'
[workspace]
members = ["util", "app"]
resolver = "2"

[workspace.package]
version = "0.1.0"
edition = "2026"
license = "MIT"

[workspace.dependencies]
util = { path = "util" }

[workspace.profile.release]
opt-level = 3
EOF

mkdir -p "${WS}/util/src"
cat > "${WS}/util/Rexy.toml" <<'EOF'
[package]
name = "util"
version.workspace = true
edition.workspace = true
license.workspace = true

[targets.lib]
path = "src/util.rx"
EOF
cat > "${WS}/util/src/util.rx" <<'EOF'
pub fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
EOF

mkdir -p "${WS}/app/src"
cat > "${WS}/app/Rexy.toml" <<'EOF'
[package]
name = "app"
version.workspace = true
edition.workspace = true

[dependencies]
util.workspace = true

[[targets.bin]]
name = "app"
path = "src/main.rx"

[[targets.test]]
name = "smoke"
path = "tests/smoke.rx"
EOF
cat > "${WS}/app/src/main.rx" <<'EOF'
mod util;
use util::add;
fn main() -> i32 { return add(1, -1); }
EOF
mkdir -p "${WS}/app/tests"
cat > "${WS}/app/tests/smoke.rx" <<'EOF'
mod util;
use util::add;
fn main() -> i32 { return add(2, -2); }   // exits 0 → test passes
EOF

# 2) Build from the member dir; inheritance must resolve.
cd "${WS}/app"
"${RXY}" --color=never build 2>&1 | tee /tmp/e_build.txt
test -x target/dev/app

# 3) Build from the workspace root with --release should warn about
#    recorded-but-not-wired profile knobs.
cd "${WS}/app"
"${RXY}" --color=never build --release 2>&1 | tee /tmp/e_release.txt
grep -q "opt-level = 3 — recorded but not yet wired into rexc" /tmp/e_release.txt
test -x target/release/app

# 4) --target produces a target-prefixed output dir.
cd "${WS}/app"
"${RXY}" --color=never build --target=arm64-macos 2>&1 | tee /tmp/e_target.txt
test -x target/arm64-macos/dev/app

# 5) rxy test runs the declared test, expects exit 0.
cd "${WS}/app"
"${RXY}" --color=never test 2>&1 | tee /tmp/e_test.txt
grep -q "test result: ok\." /tmp/e_test.txt
grep -q "1 passed; 0 failed" /tmp/e_test.txt

# 6) rxy test --target=<triple> skips the run, reports skipped count.
cd "${WS}/app"
"${RXY}" --color=never test --target=arm64-macos 2>&1 | tee /tmp/e_test_target.txt
grep -q "Skipping" /tmp/e_test_target.txt
grep -q "1 skipped" /tmp/e_test_target.txt

# 7) Failing test surfaces as exit 1.
cat > "${WS}/app/tests/smoke.rx" <<'EOF'
fn main() -> i32 { return 7; }   // non-zero exit → test fails
EOF
cd "${WS}/app"
if "${RXY}" --color=never test 2>/tmp/e_test_fail.txt; then
    echo "smoke_e: expected failing test to exit non-zero"; cat /tmp/e_test_fail.txt; exit 1
fi
grep -q "test result: FAILED" /tmp/e_test_fail.txt

# 8) Inheritance error: member references a missing workspace key.
WS2="${WORK}/ws_bad"
mkdir -p "${WS2}/m/src"
cat > "${WS2}/Rexy.toml" <<'EOF'
[workspace]
members = ["m"]

[workspace.package]
edition = "2026"
EOF
cat > "${WS2}/m/Rexy.toml" <<'EOF'
[package]
name = "m"
version.workspace = true
edition.workspace = true
EOF
echo 'fn main() -> i32 { return 0; }' > "${WS2}/m/src/main.rx"
cd "${WS2}/m"
if "${RXY}" --color=never build 2>/tmp/e_inherit_err.txt; then
    echo "smoke_e: expected inheritance error"; cat /tmp/e_inherit_err.txt; exit 1
fi
grep -q "version.workspace = true" /tmp/e_inherit_err.txt
grep -q "workspace root manifest" /tmp/e_inherit_err.txt

echo "smoke_e: all checks passed"
