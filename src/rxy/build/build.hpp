#pragma once

#include "manifest/manifest.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rxy::build {

struct Options {
    std::string profile_name = "dev";
    std::optional<std::string> bin;     // build only this bin if set
    bool quiet   = false;
    bool verbose = false;
    bool color_for_rexc = false;        // pass --color=always to rexc when true
    bool locked  = false;               // FR-011: fail if Rexy.lock would change
    bool no_build_scripts = false;      // FR-024: skip all build scripts
    bool allow_all_build_scripts = false; // PRD R-3 escape hatch for transitive
};

struct Result {
    int exit_code = 0;
    std::vector<std::filesystem::path> artifacts;
};

// Phase A: compile each bin (and lib if present) by invoking rexc once per
// target. Writes `target/<profile>/<bin-name>` artifacts.
Result run(const manifest::Manifest&, const Options&);

}  // namespace rxy::build
