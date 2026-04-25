#!/bin/sh
# Assembles compiler output as 32-bit x86 when a suitable assembler is present.
set -eu

build_dir=$1
repo_dir=$2
tmp_dir="${build_dir}/assemble-smoke"
mkdir -p "$tmp_dir"

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" -S -o "${tmp_dir}/add.s"

if command -v x86_64-elf-as >/dev/null 2>&1; then
	x86_64-elf-as --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
elif command -v as >/dev/null 2>&1; then
	as --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
else
	echo "SKIP: no GNU assembler found"
	exit 0
fi

test -s "${tmp_dir}/add.o"
