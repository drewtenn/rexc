#!/bin/sh
# Exercises the installed CLI against example Rexc programs.
set -eu

build_dir=$1
repo_dir=$2
tmp_dir="${build_dir}/cli-smoke"
mkdir -p "$tmp_dir"

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target i386 -S -o "${tmp_dir}/add.s"
test -s "${tmp_dir}/add.s"
grep -q ".globl main" "${tmp_dir}/add.s"
grep -q "call add" "${tmp_dir}/add.s"

"${build_dir}/rexc" "${repo_dir}/examples/types.rx" --target i386 -S -o "${tmp_dir}/types.s"
test -s "${tmp_dir}/types.s"
grep -F -q '.asciz "hello"' "${tmp_dir}/types.s"
grep -F -q 'movl $4000000000, %eax' "${tmp_dir}/types.s"
grep -F -q 'movl $1, %eax' "${tmp_dir}/types.s"
grep -F -q 'movl $120, %eax' "${tmp_dir}/types.s"

"${build_dir}/rexc" "${repo_dir}/examples/types.rx" --target x86_64 -S -o "${tmp_dir}/types64.s"
test -s "${tmp_dir}/types64.s"
grep -F -q 'movabsq $4000000000, %rax' "${tmp_dir}/types64.s"
grep -F -q 'leaq .Lstr0(%rip), %rax' "${tmp_dir}/types64.s"
grep -F -q 'pushq %rbp' "${tmp_dir}/types64.s"

"${build_dir}/rexc" "${repo_dir}/examples/wide.rx" --target x86_64 -S -o "${tmp_dir}/wide64.s"
test -s "${tmp_dir}/wide64.s"
grep -F -q 'movabsq $18446744073709551615, %rax' "${tmp_dir}/wide64.s"
grep -F -q 'negq %rax' "${tmp_dir}/wide64.s"

"${build_dir}/rexc" "${repo_dir}/examples/branch.rx" --target i386 -S -o "${tmp_dir}/branch32.s"
test -s "${tmp_dir}/branch32.s"
grep -F -q 'cmpl %ecx, %eax' "${tmp_dir}/branch32.s"
grep -F -q 'setl %al' "${tmp_dir}/branch32.s"
grep -F -q 'je .L_else_' "${tmp_dir}/branch32.s"

"${build_dir}/rexc" "${repo_dir}/examples/branch.rx" --target x86_64 -S -o "${tmp_dir}/branch64.s"
test -s "${tmp_dir}/branch64.s"
grep -F -q 'cmpq %rcx, %rax' "${tmp_dir}/branch64.s"
grep -F -q 'setl %al' "${tmp_dir}/branch64.s"
grep -F -q 'je .L_else_' "${tmp_dir}/branch64.s"

"${build_dir}/rexc" "${repo_dir}/examples/loop.rx" --target i386 -S -o "${tmp_dir}/loop32.s"
test -s "${tmp_dir}/loop32.s"
grep -F -q '.L_while_start_' "${tmp_dir}/loop32.s"
grep -F -q '.L_while_end_' "${tmp_dir}/loop32.s"
grep -F -q 'jmp .L_while_start_' "${tmp_dir}/loop32.s"
grep -F -q 'jmp .L_while_end_' "${tmp_dir}/loop32.s"

"${build_dir}/rexc" "${repo_dir}/examples/bool.rx" --target i386 -S -o "${tmp_dir}/bool32.s"
test -s "${tmp_dir}/bool32.s"
grep -F -q 'sete %al' "${tmp_dir}/bool32.s"
grep -F -q '.L_logic_false_' "${tmp_dir}/bool32.s"
grep -F -q '.L_logic_true_' "${tmp_dir}/bool32.s"

"${build_dir}/rexc" "${repo_dir}/examples/cast.rx" --target i386 -S -o "${tmp_dir}/cast32.s"
test -s "${tmp_dir}/cast32.s"
grep -F -q 'movzbl %al, %eax' "${tmp_dir}/cast32.s"
grep -F -q 'movl $65, %eax' "${tmp_dir}/cast32.s"

"${build_dir}/rexc" "${repo_dir}/examples/pointer.rx" --target i386 -S -o "${tmp_dir}/pointer32.s"
test -s "${tmp_dir}/pointer32.s"
grep -F -q 'leal -4(%ebp), %eax' "${tmp_dir}/pointer32.s"
grep -F -q 'movl %ecx, (%eax)' "${tmp_dir}/pointer32.s"
grep -F -q 'movl (%eax), %eax' "${tmp_dir}/pointer32.s"

"${build_dir}/rexc" "${repo_dir}/examples/pointer_index.rx" --target i386 -S -o "${tmp_dir}/pointer-index32.s"
test -s "${tmp_dir}/pointer-index32.s"
grep -F -q 'imull $4, %ecx' "${tmp_dir}/pointer-index32.s"
grep -F -q 'addl %ecx, %eax' "${tmp_dir}/pointer-index32.s"
grep -F -q 'movl (%eax), %eax' "${tmp_dir}/pointer-index32.s"

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -S -o "${tmp_dir}/add-arm64.s"
test -s "${tmp_dir}/add-arm64.s"
grep -F -q '.globl _main' "${tmp_dir}/add-arm64.s"
grep -F -q 'bl _add' "${tmp_dir}/add-arm64.s"
grep -F -q 'ret' "${tmp_dir}/add-arm64.s"
