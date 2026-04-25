#!/bin/sh
# Assembles compiler output for both x86 targets when an assembler is present.
set -eu

build_dir=$1
repo_dir=$2
tmp_dir="${build_dir}/assemble-smoke"
mkdir -p "$tmp_dir"

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" -S -o "${tmp_dir}/add.s"
"${build_dir}/rexc" "${repo_dir}/examples/wide.rx" --target x86_64 -S -o "${tmp_dir}/wide64.s"

if command -v x86_64-elf-as >/dev/null 2>&1; then
	x86_64-elf-as --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
	x86_64-elf-as --64 -o "${tmp_dir}/wide64.o" "${tmp_dir}/wide64.s"
elif command -v as >/dev/null 2>&1; then
	as --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
	as --64 -o "${tmp_dir}/wide64.o" "${tmp_dir}/wide64.s"
else
	echo "SKIP: no GNU assembler found"
	exit 0
fi

test -s "${tmp_dir}/add.o"
test -s "${tmp_dir}/wide64.o"
