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
"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -S -o "${tmp_dir}/add-arm64.s"

gnu_as=
if command -v x86_64-elf-as >/dev/null 2>&1; then
	gnu_as=x86_64-elf-as
elif command -v as >/dev/null 2>&1 && as --version 2>/dev/null | grep -qi 'gnu assembler'; then
	gnu_as=as
fi

if [ -n "$gnu_as" ]; then
	"$gnu_as" --32 -o "${tmp_dir}/add.o" "${tmp_dir}/add.s"
	"$gnu_as" --32 -o "${tmp_dir}/branch32.o" "${tmp_dir}/branch32.s"
	"$gnu_as" --64 -o "${tmp_dir}/wide64.o" "${tmp_dir}/wide64.s"
	"$gnu_as" --64 -o "${tmp_dir}/branch64.o" "${tmp_dir}/branch64.s"
	test -s "${tmp_dir}/add.o"
	test -s "${tmp_dir}/branch32.o"
	test -s "${tmp_dir}/wide64.o"
	test -s "${tmp_dir}/branch64.o"

	"${build_dir}/rexc" "${repo_dir}/examples/add.rx" -c -o "${tmp_dir}/add-cli.o"
	"${build_dir}/rexc" "${repo_dir}/examples/wide.rx" --target x86_64 -c -o "${tmp_dir}/wide64-cli.o"
	test -s "${tmp_dir}/add-cli.o"
	test -s "${tmp_dir}/wide64-cli.o"
else
	echo "SKIP: no GNU assembler found"
fi

if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
	as -arch arm64 -o "${tmp_dir}/add-arm64.o" "${tmp_dir}/add-arm64.s"
	"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -c -o "${tmp_dir}/add-arm64-cli.o"
	"${build_dir}/rexc" "${repo_dir}/examples/add.rx" --target arm64-macos -o "${tmp_dir}/add-arm64"
	test -s "${tmp_dir}/add-arm64.o"
	test -s "${tmp_dir}/add-arm64-cli.o"
	test -x "${tmp_dir}/add-arm64"
	set +e
	"${tmp_dir}/add-arm64"
	exit_code=$?
	set -e
	test "$exit_code" -eq 42
else
	echo "SKIP: arm64-macos object smoke requires Apple Silicon macOS"
fi

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
