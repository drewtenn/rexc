#!/bin/sh
set -eu

build_dir=$1
repo_dir=$2
tmp_dir="${build_dir}/cli-smoke"
mkdir -p "$tmp_dir"

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" -S -o "${tmp_dir}/add.s"
test -s "${tmp_dir}/add.s"
grep -q ".globl main" "${tmp_dir}/add.s"
grep -q "call add" "${tmp_dir}/add.s"
