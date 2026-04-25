#pragma once

// Public i386 backend API: typed IR in, assembly or diagnostics out.
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

CodegenResult emit_x86_assembly(const ir::Module &module, Diagnostics &diagnostics);

} // namespace rexc
