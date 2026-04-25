#pragma once

// Public x86 backend API: typed IR in, target assembly or diagnostics out.
#include "rexc/codegen.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/ir.hpp"

namespace rexc {

CodegenResult emit_x86_assembly(const ir::Module &module, Diagnostics &diagnostics,
                                CodegenTarget target = CodegenTarget::I386);

} // namespace rexc
