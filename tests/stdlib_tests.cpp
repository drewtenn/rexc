#include "rexc/codegen.hpp"
#include "rexc/stdlib.hpp"
#include "test_support.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

bool contains(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE(stdlib_uses_rx_files_as_canonical_source)
{
	const std::string source_dir = REXC_SOURCE_DIR;
	std::ifstream glue(source_dir + "/src/stdlib/stdlib.cpp");
	REQUIRE(glue.is_open());
	std::ostringstream glue_text;
	glue_text << glue.rdbuf();
	REQUIRE(!contains(glue_text.str(), "const char *portable_stdlib_source()"));

	std::ifstream core_str(source_dir + "/src/stdlib/core/str.rx");
	std::ifstream core_num(source_dir + "/src/stdlib/core/num.rx");
	std::ifstream std_io(source_dir + "/src/stdlib/std/io.rx");
	std::ifstream std_process(source_dir + "/src/stdlib/std/process.rx");
	REQUIRE(core_str.is_open());
	REQUIRE(core_num.is_open());
	REQUIRE(std_io.is_open());
	REQUIRE(std_process.is_open());
}

TEST_CASE(stdlib_declares_prelude_functions)
{
	auto print = rexc::stdlib::find_prelude_function("print");
	auto println = rexc::stdlib::find_prelude_function("println");
	auto read_line = rexc::stdlib::find_prelude_function("read_line");
	auto strlen = rexc::stdlib::find_prelude_function("strlen");
	auto str_eq = rexc::stdlib::find_prelude_function("str_eq");
	auto print_i32 = rexc::stdlib::find_prelude_function("print_i32");
	auto println_i32 = rexc::stdlib::find_prelude_function("println_i32");
	auto parse_i32 = rexc::stdlib::find_prelude_function("parse_i32");
	auto read_i32 = rexc::stdlib::find_prelude_function("read_i32");
	auto exit = rexc::stdlib::find_prelude_function("exit");

	REQUIRE(print != nullptr);
	REQUIRE(println != nullptr);
	REQUIRE(read_line != nullptr);
	REQUIRE(strlen != nullptr);
	REQUIRE(str_eq != nullptr);
	REQUIRE(print_i32 != nullptr);
	REQUIRE(println_i32 != nullptr);
	REQUIRE(parse_i32 != nullptr);
	REQUIRE(read_i32 != nullptr);
	REQUIRE(exit != nullptr);
	REQUIRE_EQ(print->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(print->parameters.size(), std::size_t(1));
	REQUIRE_EQ(print->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(print->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(println->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(println->parameters.size(), std::size_t(1));
	REQUIRE_EQ(println->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(println->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(read_line->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(read_line->parameters.size(), std::size_t(0));
	REQUIRE_EQ(read_line->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(strlen->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(strlen->parameters.size(), std::size_t(1));
	REQUIRE_EQ(strlen->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(strlen->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(str_eq->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(str_eq->parameters.size(), std::size_t(2));
	REQUIRE_EQ(str_eq->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_eq->parameters[1], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(str_eq->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Bool}));
	REQUIRE_EQ(print_i32->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(print_i32->parameters.size(), std::size_t(1));
	REQUIRE_EQ(print_i32->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(print_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(println_i32->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(println_i32->parameters.size(), std::size_t(1));
	REQUIRE_EQ(println_i32->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(println_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(parse_i32->layer, rexc::stdlib::Layer::Core);
	REQUIRE_EQ(parse_i32->parameters.size(), std::size_t(1));
	REQUIRE_EQ(parse_i32->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(parse_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(read_i32->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(read_i32->parameters.size(), std::size_t(0));
	REQUIRE_EQ(read_i32->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(exit->layer, rexc::stdlib::Layer::Std);
	REQUIRE_EQ(exit->parameters.size(), std::size_t(1));
	REQUIRE_EQ(exit->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(exit->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
}

TEST_CASE(stdlib_emits_hosted_runtime_symbols)
{
	auto i386 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::I386);
	auto x86_64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::X86_64);
	auto arm64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::ARM64_MACOS);

	REQUIRE(contains(i386, "print:"));
	REQUIRE(contains(i386, "println:"));
	REQUIRE(contains(i386, "read_line:"));
	REQUIRE(contains(i386, "strlen:"));
	REQUIRE(contains(i386, "str_eq:"));
	REQUIRE(contains(i386, "print_i32:"));
	REQUIRE(contains(i386, "println_i32:"));
	REQUIRE(contains(i386, "parse_i32:"));
	REQUIRE(contains(i386, "read_i32:"));
	REQUIRE(contains(i386, "exit:"));
	REQUIRE(contains(i386, "sys_write:"));
	REQUIRE(contains(i386, "sys_read:"));
	REQUIRE(contains(i386, "sys_exit:"));
	REQUIRE(contains(i386, "call sys_write"));
	REQUIRE(contains(i386, "call sys_read"));
	REQUIRE(contains(i386, "call sys_exit"));
	REQUIRE(!contains(i386, "sys_read_line:"));
	REQUIRE(contains(i386, "int $0x80"));

	REQUIRE(contains(x86_64, "print:"));
	REQUIRE(contains(x86_64, "println:"));
	REQUIRE(contains(x86_64, "read_line:"));
	REQUIRE(contains(x86_64, "strlen:"));
	REQUIRE(contains(x86_64, "str_eq:"));
	REQUIRE(contains(x86_64, "print_i32:"));
	REQUIRE(contains(x86_64, "println_i32:"));
	REQUIRE(contains(x86_64, "parse_i32:"));
	REQUIRE(contains(x86_64, "read_i32:"));
	REQUIRE(contains(x86_64, "exit:"));
	REQUIRE(contains(x86_64, "sys_write:"));
	REQUIRE(contains(x86_64, "sys_read:"));
	REQUIRE(contains(x86_64, "sys_exit:"));
	REQUIRE(contains(x86_64, "call sys_write"));
	REQUIRE(contains(x86_64, "call sys_read"));
	REQUIRE(contains(x86_64, "call sys_exit"));
	REQUIRE(!contains(x86_64, "sys_read_line:"));
	REQUIRE(contains(x86_64, "syscall"));
	REQUIRE(!contains(x86_64, "movq %rdi, %rdi"));

	REQUIRE(contains(arm64, "_print:"));
	REQUIRE(contains(arm64, "_println:"));
	REQUIRE(contains(arm64, "_read_line:"));
	REQUIRE(contains(arm64, "_strlen:"));
	REQUIRE(contains(arm64, "_str_eq:"));
	REQUIRE(contains(arm64, "_print_i32:"));
	REQUIRE(contains(arm64, "_println_i32:"));
	REQUIRE(contains(arm64, "_parse_i32:"));
	REQUIRE(contains(arm64, "_read_i32:"));
	REQUIRE(contains(arm64, "_sys_write:"));
	REQUIRE(contains(arm64, "_sys_read:"));
	REQUIRE(contains(arm64, "_sys_exit:"));
	REQUIRE(contains(arm64, "bl _sys_write"));
	REQUIRE(contains(arm64, "bl _sys_read"));
	REQUIRE(contains(arm64, "bl _sys_exit"));
	REQUIRE(!contains(arm64, "_sys_read_line:"));
	REQUIRE(contains(arm64, "bl _write"));
	REQUIRE(contains(arm64, "bl _read"));
	REQUIRE(!contains(arm64, "\tbl _exit\n"));
}

TEST_CASE(stdlib_runtime_dispatch_returns_different_target_assemblies)
{
	auto i386 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::I386);
	auto x86_64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::X86_64);
	auto arm64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::ARM64_MACOS);

	REQUIRE(i386.find("int $0x80") != std::string::npos);
	REQUIRE(x86_64.find("syscall") != std::string::npos);
	REQUIRE(arm64.find("bl _write") != std::string::npos);
	REQUIRE(i386 != x86_64);
	REQUIRE(x86_64 != arm64);
}
