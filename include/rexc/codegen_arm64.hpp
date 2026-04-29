#pragma once

// Public ARM64 backend API: typed IR in, assembly out for either
// Apple Mach-O (macOS) or Drunix ELF. The two emitters share the
// AAPCS calling convention and FP layout — they differ on symbol
// prefix, PC-relative relocations, and the read-only-string section
// directive. Pick the right entry point based on the target triple.
#include "rexc/codegen.hpp"
#include "rexc/diagnostics.hpp"
#include "rexc/ir.hpp"

namespace rexc {

CodegenResult emit_arm64_macos_assembly(const ir::Module &module,
                                        Diagnostics &diagnostics);

CodegenResult emit_arm64_drunix_assembly(const ir::Module &module,
                                         Diagnostics &diagnostics);

} // namespace rexc
