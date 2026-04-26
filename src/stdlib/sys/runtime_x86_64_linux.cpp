#include "runtime.hpp"

#include <string>

namespace rexc::stdlib {

std::string x86_64_linux_hosted_runtime_assembly()
{
	return R"(.text
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
)";
}

} // namespace rexc::stdlib
