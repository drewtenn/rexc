#include "rexc/stdlib.hpp"

#include "rexc/codegen_arm64.hpp"
#include "rexc/codegen_x86.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/lower_ir.hpp"
#include "rexc/parse.hpp"
#include "rexc/sema.hpp"
#include "rexc/source.hpp"

#include "core/library.hpp"
#include "std/library.hpp"
#include "sys/runtime.hpp"

#include <string>

namespace rexc::stdlib {
namespace {

void append_functions(std::vector<FunctionDecl> &target,
                      const std::vector<FunctionDecl> &source)
{
	target.insert(target.end(), source.begin(), source.end());
}

const char *portable_stdlib_source()
{
	return R"(static mut READ_LINE_BUFFER: [u8; 1024];

extern fn sys_write(fd: i32, buffer: str, len: i32) -> i32;
extern fn sys_read(fd: i32, buffer: *u8, len: i32) -> i32;
extern fn sys_exit(status: i32) -> i32;

fn strlen(value: str) -> i32 {
	let mut index: i32 = 0;
	while value[index] != 0 {
		index = index + 1;
	}
	return index;
}

fn str_eq(lhs: str, rhs: str) -> bool {
	let mut index: i32 = 0;
	while lhs[index] == rhs[index] {
		if lhs[index] == 0 {
			return true;
		}
		index = index + 1;
	}
	return false;
}

fn parse_i32(value: str) -> i32 {
	let mut index: i32 = 0;
	let mut result: i32 = 0;
	let mut negative: bool = false;
	if value[0] == 45 {
		negative = true;
		index = 1;
		if value[index] == 0 {
			return 0;
		}
	}
	while value[index] != 0 {
		let byte: u8 = value[index];
		if byte < 48 || byte > 57 {
			return 0;
		}
		let digit: i32 = (byte as i32) - 48;
		if negative {
			if result < -214748364 {
				return 0;
			}
			if result == -214748364 {
				if digit > 8 {
					return 0;
				}
			}
			result = result * 10 - digit;
		} else {
			if result > 214748364 {
				return 0;
			}
			if result == 214748364 {
				if digit > 7 {
					return 0;
				}
			}
			result = result * 10 + digit;
		}
		index = index + 1;
	}
	return result;
}

fn print(value: str) -> i32 {
	return sys_write(1, value, strlen(value));
}

fn println(value: str) -> i32 {
	let count: i32 = print(value);
	return count + sys_write(1, "\n", 1);
}

fn read_line() -> str {
	let mut index: i32 = 0;
	while index < 1023 {
		let count: i32 = sys_read(0, READ_LINE_BUFFER + index, 1);
		if count <= 0 {
			*(READ_LINE_BUFFER + index) = 0;
			return READ_LINE_BUFFER;
		}
		if READ_LINE_BUFFER[index] == 10 {
			*(READ_LINE_BUFFER + index) = 0;
			return READ_LINE_BUFFER;
		}
		index = index + 1;
	}
	*(READ_LINE_BUFFER + index) = 0;
	return READ_LINE_BUFFER;
}

fn print_digit(value: i32) -> i32 {
	if value == 0 { return sys_write(1, "0", 1); }
	if value == 1 { return sys_write(1, "1", 1); }
	if value == 2 { return sys_write(1, "2", 1); }
	if value == 3 { return sys_write(1, "3", 1); }
	if value == 4 { return sys_write(1, "4", 1); }
	if value == 5 { return sys_write(1, "5", 1); }
	if value == 6 { return sys_write(1, "6", 1); }
	if value == 7 { return sys_write(1, "7", 1); }
	if value == 8 { return sys_write(1, "8", 1); }
	return sys_write(1, "9", 1);
}

fn print_i32_positive(value: i32) -> i32 {
	if value >= 10 {
		let quotient: i32 = value / 10;
		let count: i32 = print_i32_positive(quotient);
		return count + print_digit(value - quotient * 10);
	}
	return print_digit(value);
}

fn print_i32_negative_digits(value: i32) -> i32 {
	if value <= -10 {
		let quotient: i32 = value / 10;
		let count: i32 = print_i32_negative_digits(quotient);
		return count + print_digit(-(value - quotient * 10));
	}
	return print_digit(-value);
}

fn print_i32(value: i32) -> i32 {
	if value == 0 {
		return print_digit(0);
	}
	if value < 0 {
		let count: i32 = sys_write(1, "-", 1);
		return count + print_i32_negative_digits(value);
	}
	return print_i32_positive(value);
}

fn println_i32(value: i32) -> i32 {
	let count: i32 = print_i32(value);
	return count + sys_write(1, "\n", 1);
}

fn read_i32() -> i32 {
	return parse_i32(read_line());
}

fn exit(status: i32) -> i32 {
	return sys_exit(status);
}
)";
}

std::string portable_stdlib_assembly(CodegenTarget target)
{
	Diagnostics diagnostics;
	SourceFile source("stdlib.rx", portable_stdlib_source());
	auto parsed = parse_source(source, diagnostics);
	if (!parsed.ok())
		return "# failed to parse stdlib.rx\n# " + diagnostics.format() + "\n";

	SemanticOptions semantic_options;
	semantic_options.include_stdlib_prelude = false;
	auto sema = analyze_module(parsed.module(), diagnostics, semantic_options);
	if (!sema.ok())
		return "# failed to analyze stdlib.rx\n# " + diagnostics.format() + "\n";

	LowerOptions lower_options;
	lower_options.include_stdlib_prelude = false;
	auto lowered = lower_to_ir(parsed.module(), lower_options);

	CodegenResult emitted =
		target == CodegenTarget::ARM64_MACOS
			? emit_arm64_macos_assembly(lowered, diagnostics)
			: emit_x86_assembly(lowered, diagnostics, target);
	if (!emitted.ok())
		return "# failed to emit stdlib.rx\n# " + diagnostics.format() + "\n";
	return emitted.assembly();
}

std::string sys_runtime_assembly(CodegenTarget target)
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

} // namespace

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions = [] {
		std::vector<FunctionDecl> result;
		append_functions(result, core::prelude_functions());
		append_functions(result, std_layer::prelude_functions());
		return result;
	}();
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
	return sys_runtime_assembly(target) + "\n" + portable_stdlib_assembly(target);
}

} // namespace rexc::stdlib
