#include "rexc/codegen.hpp"
#include "rexc/stdlib.hpp"
#include "test_support.hpp"

#include <string>

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
	REQUIRE_EQ(read_line->parameters.size(), std::size_t(0));
}

TEST_CASE(stdlib_emits_hosted_runtime_symbols)
{
	auto i386 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::I386);
	auto x86_64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::X86_64);
	auto arm64 = rexc::stdlib::hosted_runtime_assembly(rexc::CodegenTarget::ARM64_MACOS);

	REQUIRE(i386.find("print:") != std::string::npos);
	REQUIRE(i386.find("println:") != std::string::npos);
	REQUIRE(i386.find("read_line:") != std::string::npos);
	REQUIRE(i386.find("exit:") != std::string::npos);
	REQUIRE(x86_64.find("syscall") != std::string::npos);
	REQUIRE(arm64.find("_print:") != std::string::npos);
	REQUIRE(arm64.find("bl _write") != std::string::npos);
	REQUIRE(arm64.find("_read_line:") != std::string::npos);
}
