#pragma once

#include "rexc/codegen.hpp"

#include <optional>
#include <string>

namespace rexc {

enum class TargetTriple {
	I386Linux,
	I386Elf,
	X86_64Linux,
	X86_64Elf,
	ARM64Macos,
	I386Drunix,
};

std::optional<TargetTriple> parse_target_triple(const std::string &target);
CodegenTarget codegen_target(TargetTriple target);
bool is_darwin_target(TargetTriple target);
bool is_drunix_target(TargetTriple target);
const char *target_triple_name(TargetTriple target);

} // namespace rexc
