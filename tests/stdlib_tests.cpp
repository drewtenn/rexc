#include "rexc/codegen.hpp"
#include "rexc/stdlib.hpp"
#include "test_support.hpp"

#include <string>

namespace {

bool contains(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}

std::size_t count_occurrences(const std::string &text, const std::string &needle)
{
	std::size_t count = 0;
	std::size_t offset = 0;
	while ((offset = text.find(needle, offset)) != std::string::npos) {
		++count;
		offset += needle.size();
	}
	return count;
}

} // namespace

TEST_CASE(stdlib_declares_prelude_functions)
{
	auto print = rexc::stdlib::find_prelude_function("print");
	auto println = rexc::stdlib::find_prelude_function("println");
	auto read_line = rexc::stdlib::find_prelude_function("read_line");
	auto exit = rexc::stdlib::find_prelude_function("exit");

	REQUIRE(print != nullptr);
	REQUIRE(println != nullptr);
	REQUIRE(read_line != nullptr);
	REQUIRE(exit != nullptr);
	REQUIRE_EQ(print->parameters.size(), std::size_t(1));
	REQUIRE_EQ(print->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(print->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(println->parameters.size(), std::size_t(1));
	REQUIRE_EQ(println->parameters[0], (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
	REQUIRE_EQ(println->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::SignedInteger, 32}));
	REQUIRE_EQ(read_line->parameters.size(), std::size_t(0));
	REQUIRE_EQ(read_line->return_type, (rexc::PrimitiveType{rexc::PrimitiveKind::Str}));
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
	REQUIRE(contains(i386, "exit:"));
	REQUIRE(contains(i386, "int $0x80"));
	REQUIRE_EQ(count_occurrences(i386, "pushl %ebx"), std::size_t(3));
	REQUIRE_EQ(count_occurrences(i386, "popl %ebx"), std::size_t(3));

	REQUIRE(contains(x86_64, "print:"));
	REQUIRE(contains(x86_64, "println:"));
	REQUIRE(contains(x86_64, "read_line:"));
	REQUIRE(contains(x86_64, "exit:"));
	REQUIRE(contains(x86_64, "syscall"));
	REQUIRE(!contains(x86_64, "movq %rdi, %rdi"));

	REQUIRE(contains(arm64, "_print:"));
	REQUIRE(contains(arm64, "_println:"));
	REQUIRE(contains(arm64, "_read_line:"));
	REQUIRE(contains(arm64, "bl _write"));
	REQUIRE(contains(arm64, "bl _read"));
	REQUIRE(!contains(arm64, "_exit:"));
}
