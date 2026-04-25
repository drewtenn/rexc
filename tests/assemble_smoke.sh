#!/bin/sh
# Assembles compiler output for both x86 targets when an assembler is present.
set -eu

build_dir=$1
repo_dir=$2
tmp_dir="${build_dir}/assemble-smoke"
mkdir -p "$tmp_dir"

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" -S -o "${tmp_dir}/add.s"
"${build_dir}/rexc" "${repo_dir}/examples/branch.rx" --target i386 -S -o "${tmp_dir}/branch32.s"
"${build_dir}/rexc" "${repo_dir}/examples/wide.rx" --target x86_64 -S -o "${tmp_dir}/wide64.s"
"${build_dir}/rexc" "${repo_dir}/examples/branch.rx" --target x86_64 -S -o "${tmp_dir}/branch64.s"

if command -v x86_64-elf-as >/dev/null 2>&1; then
	x86_64-elf-as --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
	x86_64-elf-as --32 -o "${tmp_dir}/branch32.o" "${tmp_dir}/branch32.s"
	x86_64-elf-as --64 -o "${tmp_dir}/wide64.o" "${tmp_dir}/wide64.s"
	x86_64-elf-as --64 -o "${tmp_dir}/branch64.o" "${tmp_dir}/branch64.s"
elif command -v as >/dev/null 2>&1; then
	as --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
	as --32 -o "${tmp_dir}/branch32.o" "${tmp_dir}/branch32.s"
	as --64 -o "${tmp_dir}/wide64.o" "${tmp_dir}/wide64.s"
	as --64 -o "${tmp_dir}/branch64.o" "${tmp_dir}/branch64.s"
else
	echo "SKIP: no GNU assembler found"
	exit 0
fi

test -s "${tmp_dir}/add.o"
test -s "${tmp_dir}/branch32.o"
test -s "${tmp_dir}/wide64.o"
test -s "${tmp_dir}/branch64.o"

"${build_dir}/rexc" "${repo_dir}/examples/add.rx" -c -o "${tmp_dir}/add-cli.o"
"${build_dir}/rexc" "${repo_dir}/examples/wide.rx" --target x86_64 -c -o "${tmp_dir}/wide64-cli.o"
test -s "${tmp_dir}/add-cli.o"
test -s "${tmp_dir}/wide64-cli.o"

if command -v x86_64-elf-as >/dev/null 2>&1 &&
	command -v x86_64-elf-ld >/dev/null 2>&1; then
	drunix_dir="${tmp_dir}/fake-drunix"
	mkdir -p "${drunix_dir}/user/lib"
	cat > "${tmp_dir}/crt0.s" <<'ASM'
.globl _start
_start:
	call main
.Lhalt:
	jmp .Lhalt
ASM
	x86_64-elf-as --32 -o "${drunix_dir}/user/lib/crt0.o" "${tmp_dir}/crt0.s"
	printf '!<arch>\n' > "${drunix_dir}/user/lib/libc.a"
	cat > "${drunix_dir}/user/user.ld" <<'LDS'
ENTRY(_start)
SECTIONS
{
	. = 0x400000;
	.text : { *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	.bss : { *(.bss*) }
}
LDS

	"${build_dir}/rexc" "${repo_dir}/examples/add.rx" \
		--drunix-root "${drunix_dir}" \
		-o "${tmp_dir}/add.drunix"
	test -s "${tmp_dir}/add.drunix"
else
	echo "SKIP: no x86_64-elf-as/x86_64-elf-ld pair found for Drunix link smoke"
fi
