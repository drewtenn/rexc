#pragma once

// Public x86 backend API: typed IR in, target assembly or diagnostics out.
#include "rexc/diagnostics.hpp"
#include "rexc/ir.hpp"

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
};

CodegenResult emit_x86_assembly(const ir::Module &module, Diagnostics &diagnostics,
                                CodegenTarget target = CodegenTarget::I386);

} // namespace rexc
