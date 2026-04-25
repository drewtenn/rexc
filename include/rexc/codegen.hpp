#pragma once

// Shared code generation result and target declarations.
#include <string>

namespace rexc {

class CodegenResult {
public:
	CodegenResult(bool ok, std::string assembly);

	bool ok() const;
	const std::string &assembly() const;

private:
	bool ok_ = false;
	std::string assembly_;
};

enum class CodegenTarget {
	I386,
	X86_64,
	ARM64_MACOS,
};

} // namespace rexc
