#include "runtime.hpp"

#include <string>

namespace rexc::stdlib {

std::string i386_drunix_hosted_runtime_assembly()
{
	return R"(.data
.globl __rexc_argc
__rexc_argc:
	.long 0
.globl __rexc_argv
__rexc_argv:
	.long 0
.globl __rexc_envp
__rexc_envp:
	.long 0
__rexc_empty_string:
	.asciz ""
.text
.globl sys_write
sys_write:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	movl $4, %eax
	movl 8(%ebp), %ebx
	movl 12(%ebp), %ecx
	movl 16(%ebp), %edx
	int $0x80
	popl %ebx
	leave
	ret
.globl sys_read
sys_read:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	movl $3, %eax
	movl 8(%ebp), %ebx
	movl 12(%ebp), %ecx
	movl 16(%ebp), %edx
	int $0x80
	popl %ebx
	leave
	ret
.globl sys_exit
sys_exit:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	movl $1, %eax
	movl 8(%ebp), %ebx
	int $0x80
.Lsys_exit_hang:
	jmp .Lsys_exit_hang
.globl sys_file_open_read
sys_file_open_read:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	movl $5, %eax
	movl 8(%ebp), %ebx
	movl $0, %ecx
	movl $0, %edx
	int $0x80
	popl %ebx
	leave
	ret
.globl sys_file_create_write
sys_file_create_write:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	movl $8, %eax
	movl 8(%ebp), %ebx
	int $0x80
	popl %ebx
	leave
	ret
.globl sys_file_close
sys_file_close:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	movl $6, %eax
	movl 8(%ebp), %ebx
	int $0x80
	popl %ebx
	leave
	ret
.globl sys_args_len
sys_args_len:
	movl __rexc_argc, %eax
	ret
.globl sys_arg
sys_arg:
	pushl %ebp
	movl %esp, %ebp
	movl 8(%ebp), %ecx
	cmpl $0, %ecx
	jl .Lsys_arg_empty
	movl __rexc_argc, %eax
	cmpl %eax, %ecx
	jge .Lsys_arg_empty
	movl __rexc_argv, %eax
	movl (%eax,%ecx,4), %eax
	leave
	ret
.Lsys_arg_empty:
	movl $__rexc_empty_string, %eax
	leave
	ret
.globl sys_env_len
sys_env_len:
	movl __rexc_envp, %ecx
	xorl %eax, %eax
	testl %ecx, %ecx
	je .Lsys_env_len_done
.Lsys_env_len_loop:
	cmpl $0, (%ecx,%eax,4)
	je .Lsys_env_len_done
	addl $1, %eax
	jmp .Lsys_env_len_loop
.Lsys_env_len_done:
	ret
.globl sys_env_at
sys_env_at:
	pushl %ebp
	movl %esp, %ebp
	movl 8(%ebp), %edx
	cmpl $0, %edx
	jl .Lsys_env_at_empty
	movl __rexc_envp, %ecx
	testl %ecx, %ecx
	je .Lsys_env_at_empty
	movl (%ecx,%edx,4), %eax
	cmpl $0, %eax
	je .Lsys_env_at_empty
	leave
	ret
.Lsys_env_at_empty:
	movl $__rexc_empty_string, %eax
	leave
	ret
)";
}

} // namespace rexc::stdlib
