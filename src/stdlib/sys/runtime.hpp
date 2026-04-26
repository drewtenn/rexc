#pragma once

#include <string>

namespace rexc::stdlib {

std::string i386_hosted_runtime_assembly();
std::string x86_64_hosted_runtime_assembly();
std::string arm64_macos_hosted_runtime_assembly();

} // namespace rexc::stdlib
