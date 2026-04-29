#include "rexc/target.hpp"

namespace rexc {

std::optional<TargetTriple> parse_target_triple(const std::string &target)
{
	if (target == "i386" || target == "i386-linux" || target == "i686-linux" ||
	    target == "i686-unknown-linux-gnu")
		return TargetTriple::I386Linux;
	if (target == "i386-elf")
		return TargetTriple::I386Elf;
	if (target == "i386-drunix")
		return TargetTriple::I386Drunix;
	if (target == "x86_64" || target == "x86_64-linux" ||
	    target == "x86_64-unknown-linux-gnu")
		return TargetTriple::X86_64Linux;
	if (target == "x86_64-elf")
		return TargetTriple::X86_64Elf;
	if (target == "arm64-macos" || target == "arm64-apple-darwin" ||
	    target == "aarch64-apple-darwin")
		return TargetTriple::ARM64Macos;
	if (target == "arm64-drunix" || target == "aarch64-drunix")
		return TargetTriple::ARM64Drunix;
	return std::nullopt;
}

CodegenTarget codegen_target(TargetTriple target)
{
	switch (target) {
	case TargetTriple::I386Linux:
	case TargetTriple::I386Elf:
	case TargetTriple::I386Drunix:
		return CodegenTarget::I386;
	case TargetTriple::X86_64Linux:
	case TargetTriple::X86_64Elf:
		return CodegenTarget::X86_64;
	case TargetTriple::ARM64Macos:
		return CodegenTarget::ARM64_MACOS;
	case TargetTriple::ARM64Drunix:
		return CodegenTarget::ARM64_DRUNIX;
	}
	return CodegenTarget::I386;
}

bool is_darwin_target(TargetTriple target)
{
	return target == TargetTriple::ARM64Macos;
}

bool is_drunix_target(TargetTriple target)
{
	return target == TargetTriple::I386Drunix ||
	       target == TargetTriple::ARM64Drunix;
}

const char *target_triple_name(TargetTriple target)
{
	switch (target) {
	case TargetTriple::I386Linux:
		return "i386-linux";
	case TargetTriple::I386Elf:
		return "i386-elf";
	case TargetTriple::X86_64Linux:
		return "x86_64-linux";
	case TargetTriple::X86_64Elf:
		return "x86_64-elf";
	case TargetTriple::ARM64Macos:
		return "arm64-macos";
	case TargetTriple::I386Drunix:
		return "i386-drunix";
	case TargetTriple::ARM64Drunix:
		return "arm64-drunix";
	}
	return "unknown";
}

} // namespace rexc
