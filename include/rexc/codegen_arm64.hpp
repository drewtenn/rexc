#pragma once

// Public Darwin ARM64 backend API: typed IR in, Mach-O-compatible assembly out.
#include "rexc/codegen.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/ir.hpp"

namespace rexc {

CodegenResult emit_arm64_macos_assembly(const ir::Module &module,
                                        Diagnostics &diagnostics);

} // namespace rexc
