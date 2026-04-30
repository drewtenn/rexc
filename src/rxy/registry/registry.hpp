#pragma once

// TODO(rxy/Phase C) — see docs/prd-package-manager.md FR-014..FR-018, FR-035, FR-036.
// This is a Phase A scaffold. Calling into Registry from Phase A code is
// guaranteed to abort cleanly via rxy::todo::unimplemented_phase().

#include "todo.hpp"

namespace rxy::registry {

class Registry {
public:
    void publish(const char* /*pkg*/, const char* /*version*/) {
        ::rxy::todo::unimplemented_phase(::rxy::todo::PHASE_C_REGISTRY,
                                          "Registry::publish");
    }
    void yank(const char* /*pkg*/, const char* /*version*/) {
        ::rxy::todo::unimplemented_phase(::rxy::todo::PHASE_C_REGISTRY,
                                          "Registry::yank");
    }
};

}  // namespace rxy::registry
