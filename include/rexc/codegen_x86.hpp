#pragma once

#include "rexc/ir.hpp"

#include <string>

namespace rexc {

std::string emit_x86_assembly(const ir::Module &module);

} // namespace rexc
