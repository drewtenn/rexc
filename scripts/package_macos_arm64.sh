#!/bin/sh
# Build and package a native Apple Silicon rexc CLI with arm64-macos backend smoke.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="${repo_dir}/build/macos-arm64-release"
dist_dir="${repo_dir}/dist"
package_dir="${dist_dir}/rexc-macos-arm64"
archive="${dist_dir}/rexc-macos-arm64.tar.gz"
checksum="${archive}.sha256"

if [ "$(uname -s)" != "Darwin" ] || [ "$(uname -m)" != "arm64" ]; then
	echo "error: macOS arm64 packaging must run from a native Apple Silicon shell" >&2
	exit 1
fi

cmake --preset macos-arm64-release
cmake --build --preset macos-arm64-release
ctest --preset macos-arm64-release --output-on-failure

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -S -o "${build_dir}/add-arm64.s"
"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -c -o "${build_dir}/add-arm64.o"
file "${build_dir}/rexc" | grep -q 'Mach-O 64-bit executable arm64'
file "${build_dir}/add-arm64.o" | grep -q 'Mach-O 64-bit object arm64'

rm -rf "$package_dir" "$archive" "$checksum"
mkdir -p "$package_dir"
cp "${build_dir}/rexc" "$package_dir/rexc"
chmod 755 "$package_dir/rexc"

cat > "$package_dir/README.txt" <<'TXT'
Rexc macOS arm64 package

This package contains a native Apple Silicon rexc CLI.

Examples:
  ./rexc input.rx -S -o output-x86.s
  ./rexc input.rx --target arm64-macos -S -o output-arm64.s
  ./rexc input.rx --target arm64-macos -c -o output-arm64.o
  ./rexc input.rx --target arm64-macos -o output-arm64
TXT

(cd "$dist_dir" && tar -czf "$(basename "$archive")" rexc-macos-arm64)
shasum -a 256 "$archive" > "$checksum"

echo "Wrote $archive"
echo "Wrote $checksum"
