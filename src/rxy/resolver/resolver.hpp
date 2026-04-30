#pragma once

// TODO(rxy/Phase C) — see docs/prd-package-manager.md FR-016 (resolution).
// Phase A scaffold; resolution is a no-op (root only) until Phase B/C.

#include "todo.hpp"

namespace rxy::resolver {

inline void resolve_unimplemented() {
    ::rxy::todo::unimplemented_phase(::rxy::todo::PHASE_C_REGISTRY,
                                      "resolver::resolve");
}

}  // namespace rxy::resolver
