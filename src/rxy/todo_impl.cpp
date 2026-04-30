#include "todo.hpp"

#include "diag/diag.hpp"

#include <cstdlib>

namespace rxy::todo {

[[noreturn]] void unimplemented_phase(const char* phase, const char* feature) {
    auto d = diag::Diagnostic::error(
        std::string("phase `") + phase + "` is not implemented yet")
        .with_help(std::string("requested feature: ") + feature +
                   " — see docs/prd-package-manager.md");
    diag::print(d);
    std::exit(1);
}

}  // namespace rxy::todo
