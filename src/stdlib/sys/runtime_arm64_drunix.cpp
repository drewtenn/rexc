#include "runtime.hpp"

#include <string>

namespace rexc::stdlib {

// arm64-drunix runtime: direct syscalls via the AArch64 Linux ABI.
// Drunix's i386 syscall numbers match Linux's i386 numbers, so we
// extend the same compatibility assumption to AArch64 — these are the
// standard Linux/AArch64 system call numbers (asm-generic):
//   write=64, read=63, exit=93, nanosleep=101, clock_gettime=113,
//   openat=56, close=57, kill=129, execve=221.
// `getdents` is left as a stub returning -1 because the i386 build
// uses a Drunix-specific custom number (4008) that hasn't been
// generalised to arm64 yet; reading directories on arm64-drunix is
// tracked as a follow-up that lines up the kernel-side syscall.
//
// Calling convention: AAPCS in user code, AArch64 syscall convention
// inside the wrappers (x0-x5 carry args, x8 carries the syscall
// number, svc #0 enters the kernel, return value comes back in x0).
//
// The wrappers preserve x29/x30 across the trap so a stack walker
// always sees a frame pointer — matches what `arm64_macos_*` does
// for the libc-call wrappers and keeps the user-side ABI identical.
std::string arm64_drunix_hosted_runtime_assembly()
{
	return R"(.data
.globl __rexc_argc
__rexc_argc:
	.quad 0
.globl __rexc_argv
__rexc_argv:
	.quad 0
.globl __rexc_envp
__rexc_envp:
	.quad 0
.section .rodata, "a"
__rexc_empty_string:
	.asciz ""
.text
.globl sys_write
.p2align 2
sys_write:
	mov x8, #64
	svc #0
	ret
.globl sys_read
.p2align 2
sys_read:
	mov x8, #63
	svc #0
	ret
.globl sys_exit
.p2align 2
sys_exit:
	mov x8, #93
	svc #0
.Lsys_exit_hang:
	b .Lsys_exit_hang
.globl sys_sleep
.p2align 2
sys_sleep:
	stp x29, x30, [sp, #-48]!
	mov x29, sp
	sxtw x2, w0
	str x2, [sp, #16]
	str xzr, [sp, #24]
	str xzr, [sp, #32]
	str xzr, [sp, #40]
	add x0, sp, #16
	add x1, sp, #32
	mov x8, #101
	svc #0
	cbz w0, .Lsys_sleep_done
	ldr x0, [sp, #32]
	b .Lsys_sleep_return
.Lsys_sleep_done:
	mov x0, #0
.Lsys_sleep_return:
	ldp x29, x30, [sp], #48
	ret
.globl sys_unix_seconds
.p2align 2
sys_unix_seconds:
	stp x29, x30, [sp, #-32]!
	mov x29, sp
	mov x0, #0
	add x1, sp, #16
	str xzr, [sp, #16]
	str xzr, [sp, #24]
	mov x8, #113
	svc #0
	cmp x0, #0
	b.ne .Lsys_unix_seconds_error
	ldr x0, [sp, #16]
	b .Lsys_unix_seconds_return
.Lsys_unix_seconds_error:
	mov x0, #-1
.Lsys_unix_seconds_return:
	ldp x29, x30, [sp], #32
	ret
.globl sys_file_open_read
.p2align 2
sys_file_open_read:
	mov x3, x2
	mov x2, x1
	mov x1, x0
	mov x0, #-100
	mov x8, #56
	svc #0
	ret
.globl sys_file_create_write
.p2align 2
sys_file_create_write:
	mov x2, x1
	mov x1, x0
	mov x0, #-100
	mov w3, #1537
	mov w4, #420
	mov x8, #56
	svc #0
	ret
.globl sys_file_close
.p2align 2
sys_file_close:
	mov x8, #57
	svc #0
	ret
.globl sys_getdents
.p2align 2
sys_getdents:
	mov x0, #-1
	ret
.globl sys_kill
.p2align 2
sys_kill:
	mov x8, #129
	svc #0
	ret
.globl sys_execve
.p2align 2
sys_execve:
	mov x8, #221
	svc #0
	ret
.globl sys_trap_ud2
.p2align 2
sys_trap_ud2:
	.inst 0x00000000
	ret
.globl sys_trap_gpfault
.p2align 2
sys_trap_gpfault:
	.inst 0x00000000
	ret
.globl sys_args_len
.p2align 2
sys_args_len:
	adrp x0, __rexc_argc
	add x0, x0, :lo12:__rexc_argc
	ldr w0, [x0]
	ret
.globl sys_arg
.p2align 2
sys_arg:
	mov x2, x0
	adrp x0, __rexc_argc
	add x0, x0, :lo12:__rexc_argc
	ldr w1, [x0]
	cmp x2, #0
	b.lt .Lsys_arg_empty
	cmp w2, w1
	b.ge .Lsys_arg_empty
	adrp x0, __rexc_argv
	add x0, x0, :lo12:__rexc_argv
	ldr x0, [x0]
	ldr x0, [x0, x2, lsl #3]
	ret
.Lsys_arg_empty:
	adrp x0, __rexc_empty_string
	add x0, x0, :lo12:__rexc_empty_string
	ret
.globl sys_env_len
.p2align 2
sys_env_len:
	adrp x1, __rexc_envp
	add x1, x1, :lo12:__rexc_envp
	ldr x1, [x1]
	mov x0, #0
	cbz x1, .Lsys_env_len_done
.Lsys_env_len_loop:
	ldr x2, [x1, x0, lsl #3]
	cbz x2, .Lsys_env_len_done
	add x0, x0, #1
	b .Lsys_env_len_loop
.Lsys_env_len_done:
	ret
.globl sys_env_at
.p2align 2
sys_env_at:
	cmp w0, #0
	b.lt .Lsys_env_at_empty
	adrp x1, __rexc_envp
	add x1, x1, :lo12:__rexc_envp
	ldr x1, [x1]
	cbz x1, .Lsys_env_at_empty
	ldr x0, [x1, x0, lsl #3]
	cbz x0, .Lsys_env_at_empty
	ret
.Lsys_env_at_empty:
	adrp x0, __rexc_empty_string
	add x0, x0, :lo12:__rexc_empty_string
	ret
)";
}

} // namespace rexc::stdlib
