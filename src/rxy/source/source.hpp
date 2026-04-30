#pragma once

#include "manifest/manifest.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace rxy::source {

// A resolved on-disk location for a dependency, plus its identity for the
// lockfile. Path deps record only the resolved root; git deps record commit
// + content checksum.
struct Resolved {
    std::string name;
    std::string version;                            // from the dep's Rexy.toml [package].version
    std::filesystem::path package_root;             // directory containing the dep's Rexy.toml

    // "path+<absolute>" | "git+<url>?(tag|rev|branch)=<v>" | "registry+default"
    std::string source_token;

    // For git sources only
    std::optional<std::string> commit;              // 40-char SHA-1
    std::optional<std::string> checksum;            // sha256:... of source tree
};

struct ResolveOptions {
    bool offline = false;        // FR-012: refuse any network operation
};

// Resolve a single direct DependencySpec.
//   - Path deps: validated, normalized, manifest loaded
//   - Git deps: clone (or update) the bare repo, checkout into ~/.rxy/src,
//     compute checksum, validate the dep's Rexy.toml.
Resolved resolve(const manifest::DependencySpec& dep,
                 const std::filesystem::path& importer_package_root,
                 const ResolveOptions& opts = {});

}  // namespace rxy::source
