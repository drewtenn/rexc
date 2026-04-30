#pragma once

// TODO(rxy/Phase B) — see docs/prd-package-manager.md FR-038 (`rxy vendor`).

#include "todo.hpp"

namespace rxy::vendor {

inline void run_unimplemented() {
    ::rxy::todo::unimplemented_phase(::rxy::todo::PHASE_B_GIT_DEPS,
                                      "vendor::run");
}

}  // namespace rxy::vendor
