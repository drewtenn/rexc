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
)";
}

} // namespace rexc::stdlib
