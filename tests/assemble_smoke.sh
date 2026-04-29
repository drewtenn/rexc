#!/bin/sh
# Assembles compiler output for both x86 targets when an assembler is present.
set -eu

build_dir=$1
repo_dir=$2
tmp_dir="${build_dir}/assemble-smoke"
mkdir -p "$tmp_dir"

assert_no_stdlib_temps() {
	test ! -f "$1.stdlib.s.tmp"
	test ! -f "$1.stdlib.o.tmp"
}

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/add.s"
"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/branch32.s"
"${build_dir}/rexc" "${repo_dir}/examples/wide.rx" --target x86_64 -S -o "${tmp_dir}/wide64.s"
"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target x86_64 -S -o "${tmp_dir}/branch64.s"
"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target arm64-macos -S -o "${tmp_dir}/add-arm64.s"

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

	"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -c -o "${tmp_dir}/add-cli.o"
	"${build_dir}/rexc" "${repo_dir}/examples/wide.rx" --target x86_64 -c -o "${tmp_dir}/wide64-cli.o"
	test -s "${tmp_dir}/add-cli.o"
	test -s "${tmp_dir}/wide64-cli.o"

	if [ "$(uname -s)" != "Darwin" ] &&
		{ command -v clang >/dev/null 2>&1 || command -v cc >/dev/null 2>&1; }; then
		"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target x86_64 -o "${tmp_dir}/add-x86_64"
		test -x "${tmp_dir}/add-x86_64"
		"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" --target x86_64 -o "${tmp_dir}/stdlib-x86_64"
		test -x "${tmp_dir}/stdlib-x86_64"
		printf 'friend\nsecond\n21\n' | "${tmp_dir}/stdlib-x86_64" > "${tmp_dir}/stdlib-x86_64.out"
		grep -F -q 'hello from rexc' "${tmp_dir}/stdlib-x86_64.out"
		grep -F -q 'echo: friend' "${tmp_dir}/stdlib-x86_64.out"
		grep -F -q 'second' "${tmp_dir}/stdlib-x86_64.out"
		grep -F -q 'number?' "${tmp_dir}/stdlib-x86_64.out"
		grep -F -q 'double: 42' "${tmp_dir}/stdlib-x86_64.out"
		grep -F -q 'truefalse' "${tmp_dir}/stdlib-x86_64.out"
		grep -F -q '!?' "${tmp_dir}/stdlib-x86_64.out"
		assert_no_stdlib_temps "${tmp_dir}/stdlib-x86_64"
		if command -v clang >/dev/null 2>&1 &&
			clang -m32 -x c /dev/null -o "${tmp_dir}/empty32" >/dev/null 2>&1; then
			"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -o "${tmp_dir}/add-i386"
			test -x "${tmp_dir}/add-i386"
		else
			echo "SKIP: no clang -m32 runtime found for i386 executable smoke"
		fi
	elif [ "$(uname -s)" = "Darwin" ]; then
		if command -v x86_64-elf-ld >/dev/null 2>&1; then
			"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -o "${tmp_dir}/add-i386.elf"
			"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target x86_64 -o "${tmp_dir}/add-x86_64.elf"
			file "${tmp_dir}/add-i386.elf" | grep -F -q 'ELF 32-bit'
			file "${tmp_dir}/add-x86_64.elf" | grep -F -q 'ELF 64-bit'
			"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" --target i386 -o "${tmp_dir}/std-io-i386.elf"
			"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" --target x86_64 -o "${tmp_dir}/std-io-x86_64.elf"
			file "${tmp_dir}/std-io-i386.elf" | grep -F -q 'ELF 32-bit'
			file "${tmp_dir}/std-io-x86_64.elf" | grep -F -q 'ELF 64-bit'
			assert_no_stdlib_temps "${tmp_dir}/std-io-i386.elf"
			assert_no_stdlib_temps "${tmp_dir}/std-io-x86_64.elf"
		else
			echo "SKIP: no x86_64-elf-ld found for Darwin x86 ELF executable smoke"
		fi
	fi
else
	echo "SKIP: no GNU assembler found"
fi

if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
	as -arch arm64 -o "${tmp_dir}/add-arm64.o" "${tmp_dir}/add-arm64.s"
	"${build_dir}/rexc" "${repo_dir}/examples/core.rx" -o "${tmp_dir}/add-default"
	"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target arm64-macos -c -o "${tmp_dir}/add-arm64-cli.o"
	"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target arm64-macos -o "${tmp_dir}/add-arm64"
	test -s "${tmp_dir}/add-arm64.o"
	test -s "${tmp_dir}/add-arm64-cli.o"
	test -x "${tmp_dir}/add-default"
	test -x "${tmp_dir}/add-arm64"
	set +e
	"${tmp_dir}/add-default"
	default_exit_code=$?
	"${tmp_dir}/add-arm64"
	exit_code=$?
	set -e
	test "$default_exit_code" -eq 42
	test "$exit_code" -eq 42

	# FE-105 closure: the `Vec::with_alloc(arena)` pattern fixture.
	# Push 3 ints (10, 20, 30) through a user-owned arena and read
	# them back. Exit code is the sum, proving the round-trip.
	"${build_dir}/rexc" "${repo_dir}/examples/arena_vec_demo.rx" --target arm64-macos -o "${tmp_dir}/arena-vec-demo-arm64"
	test -x "${tmp_dir}/arena-vec-demo-arm64"
	set +e
	"${tmp_dir}/arena-vec-demo-arm64"
	arena_vec_exit=$?
	set -e
	test "$arena_vec_exit" -eq 60

	# FE-107: defer fires on early return AND on normal exit, in LIFO
	# order, with block-scoped registration. The fixture composes both
	# code paths into a single observable exit code (211). See
	# examples/defer_demo.rx for the trace derivation.
	"${build_dir}/rexc" "${repo_dir}/examples/defer_demo.rx" --target arm64-macos -o "${tmp_dir}/defer-demo-arm64"
	test -x "${tmp_dir}/defer-demo-arm64"
	set +e
	"${tmp_dir}/defer-demo-arm64"
	defer_exit=$?
	set -e
	test "$defer_exit" -eq 211

	# FE-108: definite-assignment analysis. The fixture exercises an
	# uninitialized `let mut x: i32;` whose value is set via an if/else,
	# then read after the merge — sema must accept it and the binary
	# must exit 42.
	"${build_dir}/rexc" "${repo_dir}/examples/uninit_let_demo.rx" --target arm64-macos -o "${tmp_dir}/uninit-let-demo-arm64"
	test -x "${tmp_dir}/uninit-let-demo-arm64"
	set +e
	"${tmp_dir}/uninit-let-demo-arm64"
	uninit_exit=$?
	set -e
	test "$uninit_exit" -eq 42

	# FE-109a: generic Vec<T> over a primitive (i32) and a user struct
	# (Pair). The fixture pushes 10/20/30 into Vec<i32> (sum 60) and
	# {1,2}/{3,4} into Vec<Pair> (sum 10), exit code is 70.
	"${build_dir}/rexc" "${repo_dir}/examples/generic_vec_demo.rx" --target arm64-macos -o "${tmp_dir}/generic-vec-demo-arm64"
	test -x "${tmp_dir}/generic-vec-demo-arm64"
	set +e
	"${tmp_dir}/generic-vec-demo-arm64"
	generic_vec_exit=$?
	set -e
	test "$generic_vec_exit" -eq 70

	# FE-109b: hash + equality contract for primitives + struct of
	# primitives. The fixture builds three Points, runs point_eq +
	# point_hash on them, and packs the per-check pass/fail results
	# into a single observable exit code (21). See examples/hash_demo.rx
	# for the score breakdown.
	"${build_dir}/rexc" "${repo_dir}/examples/hash_demo.rx" --target arm64-macos -o "${tmp_dir}/hash-demo-arm64"
	test -x "${tmp_dir}/hash-demo-arm64"
	set +e
	"${tmp_dir}/hash-demo-arm64"
	hash_exit=$?
	set -e
	test "$hash_exit" -eq 21

	# FE-109c: generic HashMap<K, V> with concrete ops for
	# HashMap<i32, str>. The fixture inserts three (key, value) pairs,
	# verifies len + lookup-present + value-at-slot + lookup-missing +
	# iter walks all occupied slots, packing the per-check signals
	# into a single exit code (31). See examples/hashmap_demo.rx for
	# the score breakdown.
	"${build_dir}/rexc" "${repo_dir}/examples/hashmap_demo.rx" --target arm64-macos -o "${tmp_dir}/hashmap-demo-arm64"
	test -x "${tmp_dir}/hashmap-demo-arm64"
	set +e
	"${tmp_dir}/hashmap-demo-arm64"
	hashmap_exit=$?
	set -e
	test "$hashmap_exit" -eq 31
	"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" -o "${tmp_dir}/stdlib-arm64"
	test -x "${tmp_dir}/stdlib-arm64"
	printf 'friend\nsecond\n21\n' | "${tmp_dir}/stdlib-arm64" > "${tmp_dir}/stdlib-arm64.out"
	grep -F -q 'hello from rexc' "${tmp_dir}/stdlib-arm64.out"
	grep -F -q 'echo: friend' "${tmp_dir}/stdlib-arm64.out"
	grep -F -q 'second' "${tmp_dir}/stdlib-arm64.out"
	grep -F -q 'number?' "${tmp_dir}/stdlib-arm64.out"
	grep -F -q 'double: 42' "${tmp_dir}/stdlib-arm64.out"
	grep -F -q 'truefalse' "${tmp_dir}/stdlib-arm64.out"
	grep -F -q '!?' "${tmp_dir}/stdlib-arm64.out"
	assert_no_stdlib_temps "${tmp_dir}/stdlib-arm64"
else
	echo "SKIP: arm64-macos object smoke requires Apple Silicon macOS"
fi

if command -v x86_64-elf-as >/dev/null 2>&1 &&
	command -v x86_64-elf-ld >/dev/null 2>&1; then
	drunix_dir="${tmp_dir}/fake-drunix"
	mkdir -p "${drunix_dir}/build/user/x86/linker"
	cat > "${drunix_dir}/build/user/x86/linker/user.ld" <<'LDS'
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

	"${build_dir}/rexc" "${repo_dir}/examples/core.rx" \
		--target i386-drunix \
		--drunix-root "${drunix_dir}" \
		-o "${tmp_dir}/add.drunix"
	test -s "${tmp_dir}/add.drunix"
else
	echo "SKIP: no x86_64-elf-as/x86_64-elf-ld pair found for Drunix link smoke"
fi

# arm64-drunix cross-compile smoke. Verifies the new ARM64Drunix
# target end-to-end: codegen produces ELF-flavoured aarch64 assembly,
# the cross assembler accepts it, and the cross linker produces an
# AArch64 ELF executable against a Drunix-shaped linker script. We
# don't run the binary — it would need a Drunix arm64 kernel to
# execute. The kernel-side integration is responsible for the
# runtime smoke once the binary lands in a rootfs.
if command -v aarch64-elf-as >/dev/null 2>&1 &&
	command -v aarch64-elf-ld >/dev/null 2>&1; then
	drunix_arm64_dir="${tmp_dir}/fake-drunix-arm64"
	mkdir -p "${drunix_arm64_dir}/build/user/arm64/linker"
	cat > "${drunix_arm64_dir}/build/user/arm64/linker/user.ld" <<'LDS'
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

	for demo in core.rx defer_demo.rx arena_vec_demo.rx \
	            generic_vec_demo.rx hash_demo.rx hashmap_demo.rx \
	            uninit_let_demo.rx; do
		out="${tmp_dir}/$(basename "${demo}" .rx).arm64-drunix.elf"
		"${build_dir}/rexc" "${repo_dir}/examples/${demo}" \
			--target arm64-drunix \
			--drunix-root "${drunix_arm64_dir}" \
			-o "${out}"
		test -x "${out}"
		file "${out}" | grep -F -q 'ELF 64-bit'
		file "${out}" | grep -F -q 'aarch64'
	done
else
	echo "SKIP: no aarch64-elf-as/aarch64-elf-ld pair found for arm64-drunix link smoke"
fi
