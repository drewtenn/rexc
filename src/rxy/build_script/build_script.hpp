#pragma once

// TODO(rxy/Phase D) — see docs/prd-package-manager.md FR-021..FR-025, FR-049..FR-052.

#include "todo.hpp"

namespace rxy::build_script {

inline void run_unimplemented() {
    ::rxy::todo::unimplemented_phase(::rxy::todo::PHASE_D_BUILD_SCRIPT,
                                      "build_script::run");
}

}  // namespace rxy::build_script
