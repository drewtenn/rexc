#include "runtime.hpp"

#include <string>

namespace rexc::stdlib {

std::string arm64_macos_hosted_runtime_assembly()
{
	return R"(.text
.globl _sys_write
.p2align 2
_sys_write:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	bl _write
	ldp x29, x30, [sp], #16
	ret
.globl _sys_read
.p2align 2
_sys_read:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	bl _read
	ldp x29, x30, [sp], #16
	ret
.globl _sys_exit
.p2align 2
_sys_exit:
	mov x16, #1
	svc #0x80
	ret
.globl _sys_sleep
.p2align 2
_sys_sleep:
	stp x29, x30, [sp, #-48]!
	mov x29, sp
	sxtw x2, w0
	str x2, [sp, #16]
	str xzr, [sp, #24]
	str xzr, [sp, #32]
	str xzr, [sp, #40]
	add x0, sp, #16
	add x1, sp, #32
	bl _nanosleep
	cbz w0, L_sys_sleep_done
	ldr x0, [sp, #32]
	b L_sys_sleep_return
L_sys_sleep_done:
	mov x0, #0
L_sys_sleep_return:
	ldp x29, x30, [sp], #48
	ret
.globl _sys_unix_seconds
.p2align 2
_sys_unix_seconds:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	mov x0, #0
	bl _time
	ldp x29, x30, [sp], #16
	ret
.globl _sys_file_open_read
.p2align 2
_sys_file_open_read:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	mov x1, #0
	mov x2, #0
	bl _open
	ldp x29, x30, [sp], #16
	ret
.globl _sys_file_create_write
.p2align 2
_sys_file_create_write:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	mov x1, #1537
	mov x2, #420
	bl _open
	ldp x29, x30, [sp], #16
	ret
.globl _sys_file_close
.p2align 2
_sys_file_close:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	bl _close
	ldp x29, x30, [sp], #16
	ret
.globl _sys_kill
.p2align 2
_sys_kill:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	bl _kill
	ldp x29, x30, [sp], #16
	ret
.globl _sys_execve
.p2align 2
_sys_execve:
	stp x29, x30, [sp, #-16]!
	mov x29, sp
	bl _execve
	ldp x29, x30, [sp], #16
	ret
.globl _sys_trap_ud2
.p2align 2
_sys_trap_ud2:
	.inst 0x00000000
	ret
.globl _sys_trap_gpfault
.p2align 2
_sys_trap_gpfault:
	.inst 0x00000000
	ret
.globl _sys_args_len
.p2align 2
_sys_args_len:
	adrp x0, _NXArgc@GOTPAGE
	ldr x0, [x0, _NXArgc@GOTPAGEOFF]
	ldr w0, [x0]
	ret
.globl _sys_arg
.p2align 2
_sys_arg:
	mov x2, x0
	adrp x0, _NXArgc@GOTPAGE
	ldr x0, [x0, _NXArgc@GOTPAGEOFF]
	ldr w1, [x0]
	cmp x2, #0
	b.lt L_sys_arg_empty
	cmp w2, w1
	b.ge L_sys_arg_empty
	adrp x0, _NXArgv@GOTPAGE
	ldr x0, [x0, _NXArgv@GOTPAGEOFF]
	ldr x0, [x0]
	ldr x0, [x0, x2, lsl #3]
	ret
L_sys_arg_empty:
	adrp x0, L_rexc_empty_string@PAGE
	add x0, x0, L_rexc_empty_string@PAGEOFF
	ret
.globl _sys_env_len
.p2align 2
_sys_env_len:
	adrp x1, _environ@GOTPAGE
	ldr x1, [x1, _environ@GOTPAGEOFF]
	ldr x1, [x1]
	mov x0, #0
L_sys_env_len_loop:
	ldr x2, [x1, x0, lsl #3]
	cbz x2, L_sys_env_len_done
	add x0, x0, #1
	b L_sys_env_len_loop
L_sys_env_len_done:
	ret
.globl _sys_env_at
.p2align 2
_sys_env_at:
	cmp w0, #0
	b.lt L_sys_env_at_empty
	adrp x1, _environ@GOTPAGE
	ldr x1, [x1, _environ@GOTPAGEOFF]
	ldr x1, [x1]
	ldr x0, [x1, x0, lsl #3]
	cbz x0, L_sys_env_at_empty
	ret
L_sys_env_at_empty:
	adrp x0, L_rexc_empty_string@PAGE
	add x0, x0, L_rexc_empty_string@PAGEOFF
	ret
.section __TEXT,__cstring,cstring_literals
L_rexc_empty_string:
	.asciz ""
)";
}

} // namespace rexc::stdlib
