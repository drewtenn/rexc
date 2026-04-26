#include "runtime.hpp"

#include <string>

namespace rexc::stdlib {

std::string i386_linux_hosted_runtime_assembly()
{
	return R"(.text
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
)";
}

} // namespace rexc::stdlib
