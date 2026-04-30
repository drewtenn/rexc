#pragma once

#include "util/tty.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rxy::cli {

struct GlobalFlags {
    bool quiet   = false;
    bool verbose = false;
    bool offline = false;                    // FR-012: no network operations
    util::ColorMode color = util::ColorMode::Auto;
    std::optional<std::filesystem::path> manifest_path;
};

// Entry point. Returns the exit code:
//   0   success
//   1   generic failure (build / IO / etc.)
//   2   usage error (bad args, unknown command)
//   101 internal panic (uncaught exception)
int dispatch(int argc, char** argv);

}  // namespace rxy::cli
