#pragma once

#include "manifest/manifest.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rxy::build_script {

// Parsed directives emitted by a build script via stdout `rxy:*=...` lines.
struct Directives {
    std::vector<std::filesystem::path> rerun_if_changed;
    std::vector<std::string>           rerun_if_env_changed;
    std::vector<std::filesystem::path> rxy_search_paths;
    std::map<std::string, std::string> env_overlay;
    std::vector<std::string>           warnings;
    std::vector<std::string>           cfgs;            // recorded only Phase D
    std::vector<std::string>           link_libs;       // recorded only Phase D
    std::vector<std::string>           link_searches;   // recorded only Phase D
};

struct RunOptions {
    std::filesystem::path rexc_exe;
    std::filesystem::path target_dir_root;       // package's "target" dir
    std::string profile_name;                    // "dev", "release", ...
    std::string host_triple;
    std::string target_triple;
    std::string rexc_version;                    // for cache hash
    bool quiet = false;
    bool no_build_scripts = false;
};

// Run (or fetch from cache) the build script for `pkg`. Returns parsed
// directives. If `pkg.build` is absent, returns an empty Directives.
//
// Throws std::runtime_error on:
//   - script source not found
//   - script compilation failure
//   - script runtime non-zero exit
//   - source-tree mutation by the script (FR-025 hard error)
Directives run_for(const manifest::Manifest& pkg, const RunOptions&);

// Returns the names of (direct + transitive) deps that have a build script
// but are not in `root.build.allow_scripts`. Empty result = nothing blocked.
std::vector<std::string>
disallowed_transitive_scripts(const manifest::Manifest& root,
                                const std::vector<manifest::Manifest>& deps);

// Diagnostic text for a blocked transitive build script. Multi-line per
// FR-045's "actionable" requirement.
std::string blocked_diagnostic_text(const std::vector<std::string>& blocked,
                                     const std::vector<manifest::Manifest>& deps);

// Parse a single buffer of stdout into Directives. Used by tests.
Directives parse_stdout(const std::string&);

// Cache-hash inputs (for tests).
std::string cache_hash(const manifest::Manifest&, const RunOptions&,
                        const std::string& script_src_text);

}  // namespace rxy::build_script
