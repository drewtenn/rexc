#!/bin/sh
# Exercises the installed CLI against example Rexy programs.
set -eu

build_dir=$1
repo_dir=$2
tmp_dir="${build_dir}/cli-smoke"
mkdir -p "$tmp_dir"

find "${repo_dir}/examples" -type f -name '*.rx' | sort | while IFS= read -r example; do
	if ! grep -Eq '^[[:space:]]*fn[[:space:]]+main[[:space:]]*\(' "$example"; then
		continue
	fi
	relative=${example#"${repo_dir}/examples/"}
	output_name=$(printf '%s' "$relative" | sed 's|/|_|g; s|\.rx$||')
	"${build_dir}/rexc" "$example" --target x86_64 -S -o "${tmp_dir}/example-${output_name}-x86_64.s"
	test -s "${tmp_dir}/example-${output_name}-x86_64.s"
done

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/add.s"
test -s "${tmp_dir}/add.s"
grep -q ".globl main" "${tmp_dir}/add.s"
grep -q "call add" "${tmp_dir}/add.s"

"${build_dir}/rexc" "${repo_dir}/examples/modules/main.rx" --target i386 -S -o "${tmp_dir}/modules.s"
test -s "${tmp_dir}/modules.s"
grep -F -q ".globl math_add" "${tmp_dir}/modules.s"
grep -F -q "call math_add" "${tmp_dir}/modules.s"
grep -F -q ".globl math_double" "${tmp_dir}/modules.s"
grep -F -q "call math_double" "${tmp_dir}/modules.s"
grep -F -q "use math::add;" "${repo_dir}/examples/modules/main.rx"
grep -F -q "math::double(21)" "${repo_dir}/examples/modules/main.rx"

mkdir -p "${tmp_dir}/package-entry" "${tmp_dir}/package-root-a" "${tmp_dir}/package-root-b"
cat > "${tmp_dir}/package-entry/main.rx" <<'RX'
pub mod math;
pub mod util;

fn main() -> i32 {
    return math::add(util::one(), 41);
}
RX
cat > "${tmp_dir}/package-root-a/math.rx" <<'RX'
pub fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
RX
cat > "${tmp_dir}/package-root-b/util.rx" <<'RX'
pub fn one() -> i32 {
    return 1;
}
RX
"${build_dir}/rexc" "${tmp_dir}/package-entry/main.rx" --package-path "${tmp_dir}/package-root-a" --package-path "${tmp_dir}/package-root-b" --target i386 -S -o "${tmp_dir}/package-path.s"
test -s "${tmp_dir}/package-path.s"
grep -F -q "call math_add" "${tmp_dir}/package-path.s"
grep -F -q "call util_one" "${tmp_dir}/package-path.s"

if "${build_dir}/rexc" "${repo_dir}/examples/core.rx" --package-path "${tmp_dir}/missing-package-root" --target i386 -S -o "${tmp_dir}/bad-package-path.s" 2> "${tmp_dir}/bad-package-path.err"; then
	echo "expected package path validation to fail" >&2
	exit 1
fi
grep -F -q "package path is not a directory: ${tmp_dir}/missing-package-root" "${tmp_dir}/bad-package-path.err"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/types.s"
test -s "${tmp_dir}/types.s"
grep -F -q '.asciz "hello"' "${tmp_dir}/types.s"
grep -F -q 'movl $4000000000, %eax' "${tmp_dir}/types.s"
grep -F -q 'movl $1, %eax' "${tmp_dir}/types.s"
grep -F -q 'movl $120, %eax' "${tmp_dir}/types.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target x86_64 -S -o "${tmp_dir}/types64.s"
test -s "${tmp_dir}/types64.s"
grep -F -q 'movabsq $4000000000, %rax' "${tmp_dir}/types64.s"
grep -F -q 'leaq .Lstr0(%rip), %rax' "${tmp_dir}/types64.s"
grep -F -q 'pushq %rbp' "${tmp_dir}/types64.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386-linux -S -o "${tmp_dir}/add-i386-linux.s"
test -s "${tmp_dir}/add-i386-linux.s"
grep -q ".globl main" "${tmp_dir}/add-i386-linux.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386-drunix -S -o "${tmp_dir}/add-i386-drunix.s"
test -s "${tmp_dir}/add-i386-drunix.s"
grep -q ".globl main" "${tmp_dir}/add-i386-drunix.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target x86_64-linux -S -o "${tmp_dir}/add-x86_64-linux.s"
test -s "${tmp_dir}/add-x86_64-linux.s"
grep -q ".globl main" "${tmp_dir}/add-x86_64-linux.s"

"${build_dir}/rexc" "${repo_dir}/examples/wide.rx" --target x86_64 -S -o "${tmp_dir}/wide64.s"
test -s "${tmp_dir}/wide64.s"
grep -F -q 'movabsq $18446744073709551615, %rax' "${tmp_dir}/wide64.s"
grep -F -q 'negq %rax' "${tmp_dir}/wide64.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/branch32.s"
test -s "${tmp_dir}/branch32.s"
grep -F -q 'cmpl %ecx, %eax' "${tmp_dir}/branch32.s"
grep -F -q 'setl %al' "${tmp_dir}/branch32.s"
grep -F -q 'je .L_else_' "${tmp_dir}/branch32.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target x86_64 -S -o "${tmp_dir}/branch64.s"
test -s "${tmp_dir}/branch64.s"
grep -F -q 'cmpq %rcx, %rax' "${tmp_dir}/branch64.s"
grep -F -q 'setl %al' "${tmp_dir}/branch64.s"
grep -F -q 'je .L_else_' "${tmp_dir}/branch64.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/loop32.s"
test -s "${tmp_dir}/loop32.s"
grep -F -q '.L_while_start_' "${tmp_dir}/loop32.s"
grep -F -q '.L_while_end_' "${tmp_dir}/loop32.s"
grep -F -q 'jmp .L_while_start_' "${tmp_dir}/loop32.s"
grep -F -q 'jmp .L_while_end_' "${tmp_dir}/loop32.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/bool32.s"
test -s "${tmp_dir}/bool32.s"
grep -F -q 'sete %al' "${tmp_dir}/bool32.s"
grep -F -q '.L_logic_false_' "${tmp_dir}/bool32.s"
grep -F -q '.L_logic_true_' "${tmp_dir}/bool32.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/cast32.s"
test -s "${tmp_dir}/cast32.s"
grep -F -q 'movzbl %al, %eax' "${tmp_dir}/cast32.s"
grep -F -q 'movl $65, %eax' "${tmp_dir}/cast32.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/pointer32.s"
test -s "${tmp_dir}/pointer32.s"
grep -F -q 'leal -4(%ebp), %eax' "${tmp_dir}/pointer32.s"
grep -F -q 'movl %ecx, (%eax)' "${tmp_dir}/pointer32.s"
grep -F -q 'movl (%eax), %eax' "${tmp_dir}/pointer32.s"

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target i386 -S -o "${tmp_dir}/pointer-index32.s"
test -s "${tmp_dir}/pointer-index32.s"
grep -F -q 'imull $4, %ecx' "${tmp_dir}/pointer-index32.s"
grep -F -q 'addl %ecx, %eax' "${tmp_dir}/pointer-index32.s"
grep -F -q 'movl (%eax), %eax' "${tmp_dir}/pointer-index32.s"

"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" --target i386 -S -o "${tmp_dir}/std-strings32.s"
grep -F -q 'call str_eq' "${tmp_dir}/std-strings32.s"
grep -F -q 'call strlen' "${tmp_dir}/std-strings32.s"
grep -F -q 'call str_starts_with' "${tmp_dir}/std-strings32.s"
grep -F -q 'call str_ends_with' "${tmp_dir}/std-strings32.s"
grep -F -q 'call str_contains' "${tmp_dir}/std-strings32.s"
grep -F -q 'call str_find' "${tmp_dir}/std-strings32.s"
grep -F -q 'call str_is_empty' "${tmp_dir}/std-strings32.s"

"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" --target x86_64 -S -o "${tmp_dir}/std-strings64.s"
grep -F -q 'call str_eq' "${tmp_dir}/std-strings64.s"
grep -F -q 'call strlen' "${tmp_dir}/std-strings64.s"
grep -F -q 'call str_starts_with' "${tmp_dir}/std-strings64.s"
grep -F -q 'call str_ends_with' "${tmp_dir}/std-strings64.s"
grep -F -q 'call str_contains' "${tmp_dir}/std-strings64.s"
grep -F -q 'call str_find' "${tmp_dir}/std-strings64.s"
grep -F -q 'call str_is_empty' "${tmp_dir}/std-strings64.s"

"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" --target arm64-macos -S -o "${tmp_dir}/std-strings-arm64.s"
grep -F -q 'bl _str_eq' "${tmp_dir}/std-strings-arm64.s"
grep -F -q 'bl _strlen' "${tmp_dir}/std-strings-arm64.s"
grep -F -q 'bl _str_starts_with' "${tmp_dir}/std-strings-arm64.s"
grep -F -q 'bl _str_ends_with' "${tmp_dir}/std-strings-arm64.s"
grep -F -q 'bl _str_contains' "${tmp_dir}/std-strings-arm64.s"
grep -F -q 'bl _str_find' "${tmp_dir}/std-strings-arm64.s"
grep -F -q 'bl _str_is_empty' "${tmp_dir}/std-strings-arm64.s"

"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" --target i386 -S -o "${tmp_dir}/std-numbers32.s"
grep -F -q 'call read_i32' "${tmp_dir}/std-numbers32.s"
grep -F -q 'call println_i32' "${tmp_dir}/std-numbers32.s"
grep -F -q 'call parse_i32' "${tmp_dir}/std-numbers32.s"

"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" --target x86_64 -S -o "${tmp_dir}/std-numbers64.s"
grep -F -q 'call read_i32' "${tmp_dir}/std-numbers64.s"
grep -F -q 'call println_i32' "${tmp_dir}/std-numbers64.s"
grep -F -q 'call parse_i32' "${tmp_dir}/std-numbers64.s"

"${build_dir}/rexc" "${repo_dir}/examples/stdlib.rx" --target arm64-macos -S -o "${tmp_dir}/std-numbers-arm64.s"
grep -F -q 'bl _read_i32' "${tmp_dir}/std-numbers-arm64.s"
grep -F -q 'bl _println_i32' "${tmp_dir}/std-numbers-arm64.s"
grep -F -q 'bl _parse_i32' "${tmp_dir}/std-numbers-arm64.s"

cat > "${tmp_dir}/std-panic.rx" <<'RX'
fn main() -> i32 {
    return panic("boom");
}
RX

"${build_dir}/rexc" "${tmp_dir}/std-panic.rx" --target i386 -S -o "${tmp_dir}/std-panic32.s"
grep -F -q 'call panic' "${tmp_dir}/std-panic32.s"

"${build_dir}/rexc" "${tmp_dir}/std-panic.rx" --target x86_64 -S -o "${tmp_dir}/std-panic64.s"
grep -F -q 'call panic' "${tmp_dir}/std-panic64.s"

"${build_dir}/rexc" "${tmp_dir}/std-panic.rx" --target arm64-macos -S -o "${tmp_dir}/std-panic-arm64.s"
grep -F -q 'bl _panic' "${tmp_dir}/std-panic-arm64.s"

if [ "$(uname -s)" = "Darwin" ] && [ "$(uname -m)" = "arm64" ]; then
cat > "${tmp_dir}/std-i32-edges.rx" <<'RX'
fn main() -> i32 {
    println_i32(0);
    println_i32(42);
    println_i32(-7);
    println_i32(parse_i32("2147483647"));
    println_i32(parse_i32("2147483648"));
    println_i32(parse_i32("-2147483648"));
    println_i32(parse_i32("-2147483649"));
    println_i32(parse_i32("12x"));
    return 0;
}
RX
	"${build_dir}/rexc" "${tmp_dir}/std-i32-edges.rx" --target arm64-macos -o "${tmp_dir}/std-i32-edges-arm64"
	"${tmp_dir}/std-i32-edges-arm64" > "${tmp_dir}/std-i32-edges-arm64.out"
	grep -F -q '0' "${tmp_dir}/std-i32-edges-arm64.out"
	grep -F -q '42' "${tmp_dir}/std-i32-edges-arm64.out"
	grep -F -q -- '-7' "${tmp_dir}/std-i32-edges-arm64.out"
	grep -F -q '2147483647' "${tmp_dir}/std-i32-edges-arm64.out"
	grep -F -q -- '-2147483648' "${tmp_dir}/std-i32-edges-arm64.out"
	test "$(grep -F -x -c '0' "${tmp_dir}/std-i32-edges-arm64.out")" -eq 4
fi

"${build_dir}/rexc" "${repo_dir}/examples/core.rx" --target arm64-macos -S -o "${tmp_dir}/add-arm64.s"
test -s "${tmp_dir}/add-arm64.s"
grep -F -q '.globl _main' "${tmp_dir}/add-arm64.s"
grep -F -q 'bl _add' "${tmp_dir}/add-arm64.s"
grep -F -q 'ret' "${tmp_dir}/add-arm64.s"
