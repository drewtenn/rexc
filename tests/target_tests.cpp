#include "rexc/target.hpp"

#include "test_support.hpp"

TEST_CASE(target_parser_preserves_runtime_triples)
{
	auto i386_linux = rexc::parse_target_triple("i386-linux");
	auto i386_elf = rexc::parse_target_triple("i386-elf");
	auto x86_64_linux = rexc::parse_target_triple("x86_64-linux");
	auto x86_64_elf = rexc::parse_target_triple("x86_64-elf");
	auto arm64_macos = rexc::parse_target_triple("arm64-macos");
	auto i386_drunix = rexc::parse_target_triple("i386-drunix");

	REQUIRE(i386_linux.has_value());
	REQUIRE(i386_elf.has_value());
	REQUIRE(x86_64_linux.has_value());
	REQUIRE(x86_64_elf.has_value());
	REQUIRE(arm64_macos.has_value());
	REQUIRE(i386_drunix.has_value());
	REQUIRE_EQ(*i386_linux, rexc::TargetTriple::I386Linux);
	REQUIRE_EQ(*i386_elf, rexc::TargetTriple::I386Elf);
	REQUIRE_EQ(*x86_64_linux, rexc::TargetTriple::X86_64Linux);
	REQUIRE_EQ(*x86_64_elf, rexc::TargetTriple::X86_64Elf);
	REQUIRE_EQ(*arm64_macos, rexc::TargetTriple::ARM64Macos);
	REQUIRE_EQ(*i386_drunix, rexc::TargetTriple::I386Drunix);
}

TEST_CASE(target_aliases_keep_existing_defaults)
{
	auto i386 = rexc::parse_target_triple("i386");
	auto i686_linux = rexc::parse_target_triple("i686-linux");
	auto x86_64 = rexc::parse_target_triple("x86_64");
	auto arm64_darwin = rexc::parse_target_triple("aarch64-apple-darwin");
	auto unknown = rexc::parse_target_triple("riscv64-linux");

	REQUIRE(i386.has_value());
	REQUIRE(i686_linux.has_value());
	REQUIRE(x86_64.has_value());
	REQUIRE(arm64_darwin.has_value());
	REQUIRE(!unknown.has_value());
	REQUIRE_EQ(*i386, rexc::TargetTriple::I386Linux);
	REQUIRE_EQ(*i686_linux, rexc::TargetTriple::I386Linux);
	REQUIRE_EQ(*x86_64, rexc::TargetTriple::X86_64Linux);
	REQUIRE_EQ(*arm64_darwin, rexc::TargetTriple::ARM64Macos);
}

TEST_CASE(target_triples_map_to_codegen_architectures)
{
	REQUIRE_EQ(rexc::codegen_target(rexc::TargetTriple::I386Linux),
	           rexc::CodegenTarget::I386);
	REQUIRE_EQ(rexc::codegen_target(rexc::TargetTriple::I386Elf),
	           rexc::CodegenTarget::I386);
	REQUIRE_EQ(rexc::codegen_target(rexc::TargetTriple::I386Drunix),
	           rexc::CodegenTarget::I386);
	REQUIRE_EQ(rexc::codegen_target(rexc::TargetTriple::X86_64Linux),
	           rexc::CodegenTarget::X86_64);
	REQUIRE_EQ(rexc::codegen_target(rexc::TargetTriple::X86_64Elf),
	           rexc::CodegenTarget::X86_64);
	REQUIRE_EQ(rexc::codegen_target(rexc::TargetTriple::ARM64Macos),
	           rexc::CodegenTarget::ARM64_MACOS);
}
