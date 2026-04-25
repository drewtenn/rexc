#!/bin/sh
# Exercises the installed CLI against example Rexc programs.
set -eu

build_dir=$1
repo_dir=$2
tmp_dir="${build_dir}/cli-smoke"
mkdir -p "$tmp_dir"

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" -S -o "${tmp_dir}/add.s"
test -s "${tmp_dir}/add.s"
grep -q ".globl main" "${tmp_dir}/add.s"
grep -q "call add" "${tmp_dir}/add.s"

"${build_dir}/rexc" "${repo_dir}/examples/types.rx" -S -o "${tmp_dir}/types.s"
test -s "${tmp_dir}/types.s"
grep -F -q '.asciz "hello"' "${tmp_dir}/types.s"
grep -F -q 'movl $4000000000, %eax' "${tmp_dir}/types.s"
grep -F -q 'movl $1, %eax' "${tmp_dir}/types.s"
grep -F -q 'movl $120, %eax' "${tmp_dir}/types.s"
