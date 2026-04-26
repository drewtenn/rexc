#include "rexc/stdlib.hpp"

#include <string>

namespace rexc::stdlib {
namespace {

PrimitiveType i32_type()
{
	return PrimitiveType{PrimitiveKind::SignedInteger, 32};
}

PrimitiveType str_type()
{
	return PrimitiveType{PrimitiveKind::Str};
}

std::string i386_hosted_runtime_assembly()
{
	return R"(.section .rodata
.Lrexc_newline:
	.byte 10
.section .bss
.Lrexc_read_line_buffer:
	.zero 1024
.text
.globl print
print:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	movl 8(%ebp), %ecx
	xorl %edx, %edx
.Lrexc_i386_print_len:
	cmpb $0, (%ecx,%edx)
	je .Lrexc_i386_print_write
	incl %edx
	jmp .Lrexc_i386_print_len
.Lrexc_i386_print_write:
	movl $4, %eax
	movl $1, %ebx
	int $0x80
	popl %ebx
	leave
	ret
.globl println
println:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	subl $4, %esp
	pushl 8(%ebp)
	call print
	addl $4, %esp
	movl %eax, -8(%ebp)
	movl $4, %eax
	movl $1, %ebx
	movl $.Lrexc_newline, %ecx
	movl $1, %edx
	int $0x80
	addl -8(%ebp), %eax
	addl $4, %esp
	popl %ebx
	leave
	ret
.globl read_line
read_line:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	movl $3, %eax
	movl $0, %ebx
	movl $.Lrexc_read_line_buffer, %ecx
	movl $1023, %edx
	int $0x80
	cmpl $0, %eax
	jle .Lrexc_i386_read_empty
	movl %eax, %edx
	decl %edx
	cmpb $10, .Lrexc_read_line_buffer(%edx)
	jne .Lrexc_i386_read_terminate
	movb $0, .Lrexc_read_line_buffer(%edx)
	jmp .Lrexc_i386_read_done
.Lrexc_i386_read_terminate:
	movb $0, .Lrexc_read_line_buffer(%eax)
	jmp .Lrexc_i386_read_done
.Lrexc_i386_read_empty:
	movb $0, .Lrexc_read_line_buffer
.Lrexc_i386_read_done:
	movl $.Lrexc_read_line_buffer, %eax
	popl %ebx
	leave
	ret
.globl exit
exit:
	movl 4(%esp), %ebx
	movl $1, %eax
	int $0x80
)";
}

std::string x86_64_hosted_runtime_assembly()
{
	return R"(.section .rodata
.Lrexc_newline:
	.byte 10
.section .bss
.Lrexc_read_line_buffer:
	.zero 1024
.text
.globl print
print:
	movq %rdi, %rsi
	xorq %rdx, %rdx
.Lrexc_x64_print_len:
	cmpb $0, (%rsi,%rdx)
	je .Lrexc_x64_print_write
	incq %rdx
	jmp .Lrexc_x64_print_len
.Lrexc_x64_print_write:
	movq $1, %rax
	movq $1, %rdi
	syscall
	ret
.globl println
println:
	pushq %rdi
	call print
	movq %rax, %r8
	movq $1, %rax
	movq $1, %rdi
	leaq .Lrexc_newline(%rip), %rsi
	movq $1, %rdx
	syscall
	addq %r8, %rax
	popq %rdi
	ret
.globl read_line
read_line:
	movq $0, %rax
	movq $0, %rdi
	leaq .Lrexc_read_line_buffer(%rip), %rsi
	movq $1023, %rdx
	syscall
	cmpq $0, %rax
	jle .Lrexc_x64_read_empty
	movq %rax, %rdx
	decq %rdx
	leaq .Lrexc_read_line_buffer(%rip), %rsi
	cmpb $10, (%rsi,%rdx)
	jne .Lrexc_x64_read_terminate
	movb $0, (%rsi,%rdx)
	jmp .Lrexc_x64_read_done
.Lrexc_x64_read_terminate:
	movb $0, (%rsi,%rax)
	jmp .Lrexc_x64_read_done
.Lrexc_x64_read_empty:
	leaq .Lrexc_read_line_buffer(%rip), %rsi
	movb $0, (%rsi)
.Lrexc_x64_read_done:
	leaq .Lrexc_read_line_buffer(%rip), %rax
	ret
.globl exit
exit:
	movq $60, %rax
	syscall
)";
}

std::string arm64_macos_hosted_runtime_assembly()
{
	return R"(.cstring
Lrexc_newline:
	.byte 10
.zerofill __DATA,__bss,Lrexc_read_line_buffer,1024,4
.text
.globl _print
.p2align 2
_print:
	stp x29, x30, [sp, #-32]!
	mov x29, sp
	str x19, [sp, #16]
	mov x19, x0
	mov x1, x0
	mov x2, #0
Lrexc_arm64_print_len:
	ldrb w3, [x1, x2]
	cbz w3, Lrexc_arm64_print_write
	add x2, x2, #1
	b Lrexc_arm64_print_len
Lrexc_arm64_print_write:
	mov x0, #1
	mov x1, x19
	bl _write
	ldr x19, [sp, #16]
	ldp x29, x30, [sp], #32
	ret
.globl _println
.p2align 2
_println:
	stp x29, x30, [sp, #-32]!
	mov x29, sp
	str x19, [sp, #16]
	bl _print
	mov x19, x0
	mov x0, #1
	adrp x1, Lrexc_newline@PAGE
	add x1, x1, Lrexc_newline@PAGEOFF
	mov x2, #1
	bl _write
	add x0, x19, x0
	ldr x19, [sp, #16]
	ldp x29, x30, [sp], #32
	ret
.globl _read_line
.p2align 2
_read_line:
	stp x29, x30, [sp, #-48]!
	mov x29, sp
	str x19, [sp, #16]
	str x20, [sp, #24]
	mov x0, #0
	adrp x1, Lrexc_read_line_buffer@PAGE
	add x1, x1, Lrexc_read_line_buffer@PAGEOFF
	mov x19, x1
	mov x2, #1023
	bl _read
	cmp x0, #0
	b.le Lrexc_arm64_read_empty
	mov x20, x0
	sub x2, x20, #1
	ldrb w3, [x19, x2]
	cmp w3, #10
	b.ne Lrexc_arm64_read_terminate
	strb wzr, [x19, x2]
	b Lrexc_arm64_read_done
Lrexc_arm64_read_terminate:
	strb wzr, [x19, x20]
	b Lrexc_arm64_read_done
Lrexc_arm64_read_empty:
	strb wzr, [x19]
Lrexc_arm64_read_done:
	mov x0, x19
	ldr x20, [sp, #24]
	ldr x19, [sp, #16]
	ldp x29, x30, [sp], #48
	ret
)";
}

} // namespace

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions{
		FunctionDecl{"print", {str_type()}, i32_type()},
		FunctionDecl{"println", {str_type()}, i32_type()},
		FunctionDecl{"read_line", {}, str_type()},
		FunctionDecl{"exit", {i32_type()}, i32_type()},
	};
	return functions;
}

const FunctionDecl *find_prelude_function(const std::string &name)
{
	for (const auto &function : prelude_functions()) {
		if (function.name == name)
			return &function;
	}
	return nullptr;
}

std::string hosted_runtime_assembly(CodegenTarget target)
{
	switch (target) {
	case CodegenTarget::I386:
		return i386_hosted_runtime_assembly();
	case CodegenTarget::X86_64:
		return x86_64_hosted_runtime_assembly();
	case CodegenTarget::ARM64_MACOS:
		return arm64_macos_hosted_runtime_assembly();
	}
	return "";
}

} // namespace rexc::stdlib
