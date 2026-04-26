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
)";
}

} // namespace rexc::stdlib
