#pragma once

// TODO(rxy/Phase E) — see docs/prd-package-manager.md FR-006 (workspaces).

#include "todo.hpp"

namespace rxy::workspace {

inline void load_unimplemented() {
    ::rxy::todo::unimplemented_phase(::rxy::todo::PHASE_E_WORKSPACE,
                                      "workspace::load");
}

}  // namespace rxy::workspace
