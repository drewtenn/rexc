#include "runtime.hpp"

#include <string>

namespace rexc::stdlib {

std::string x86_64_linux_hosted_runtime_assembly()
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
__rexc_empty_string:
	.asciz ""
.text
.globl sys_write
sys_write:
	movq $1, %rax
	syscall
	ret
.globl sys_read
sys_read:
	movq $0, %rax
	syscall
	ret
.globl sys_exit
sys_exit:
	movq $60, %rax
	syscall
.globl sys_sleep
sys_sleep:
	subq $32, %rsp
	movslq %edi, %rax
	movq %rax, 0(%rsp)
	movq $0, 8(%rsp)
	movq $0, 16(%rsp)
	movq $0, 24(%rsp)
	movq $35, %rax
	leaq 0(%rsp), %rdi
	leaq 16(%rsp), %rsi
	syscall
	testq %rax, %rax
	je .Lsys_sleep_done
	movq 16(%rsp), %rax
	jmp .Lsys_sleep_return
.Lsys_sleep_done:
	xorq %rax, %rax
.Lsys_sleep_return:
	addq $32, %rsp
	ret
.globl sys_unix_seconds
sys_unix_seconds:
	subq $16, %rsp
	movq $228, %rax
	movq $0, %rdi
	movq %rsp, %rsi
	syscall
	testq %rax, %rax
	jne .Lsys_unix_seconds_error
	movq 0(%rsp), %rax
	jmp .Lsys_unix_seconds_return
.Lsys_unix_seconds_error:
	movq $-1, %rax
.Lsys_unix_seconds_return:
	addq $16, %rsp
	ret
.globl sys_file_open_read
sys_file_open_read:
	movq $2, %rax
	movq $0, %rsi
	movq $0, %rdx
	syscall
	ret
.globl sys_file_create_write
sys_file_create_write:
	movq $2, %rax
	movq $577, %rsi
	movq $420, %rdx
	syscall
	ret
.globl sys_file_close
sys_file_close:
	movq $3, %rax
	syscall
	ret
.globl sys_kill
sys_kill:
	movq $62, %rax
	syscall
	ret
.globl sys_execve
sys_execve:
	movq $59, %rax
	syscall
	ret
.globl sys_args_len
sys_args_len:
	movq __rexc_argc(%rip), %rax
	ret
.globl sys_arg
sys_arg:
	cmpq $0, %rdi
	jl .Lsys_arg_empty
	movq __rexc_argc(%rip), %rax
	cmpq %rax, %rdi
	jge .Lsys_arg_empty
	movq __rexc_argv(%rip), %rax
	movq (%rax,%rdi,8), %rax
	ret
.Lsys_arg_empty:
	leaq __rexc_empty_string(%rip), %rax
	ret
.globl sys_env_len
sys_env_len:
	movq __rexc_envp(%rip), %rcx
	xorq %rax, %rax
.Lsys_env_len_loop:
	cmpq $0, (%rcx,%rax,8)
	je .Lsys_env_len_done
	addq $1, %rax
	jmp .Lsys_env_len_loop
.Lsys_env_len_done:
	ret
.globl sys_env_at
sys_env_at:
	cmpq $0, %rdi
	jl .Lsys_env_at_empty
	movq __rexc_envp(%rip), %rcx
	movq (%rcx,%rdi,8), %rax
	cmpq $0, %rax
	je .Lsys_env_at_empty
	ret
.Lsys_env_at_empty:
	leaq __rexc_empty_string(%rip), %rax
	ret
)";
}

} // namespace rexc::stdlib
