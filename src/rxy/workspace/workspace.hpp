#pragma once

#include "manifest/manifest.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rxy::workspace {

struct Workspace {
    std::filesystem::path root;                                // dir containing the workspace root Rexy.toml
    std::filesystem::path manifest_path;                       // absolute path to the workspace root manifest
    std::vector<std::filesystem::path> members;                // resolved absolute paths
    int resolver_version = 2;

    // [workspace.package] — defaults inheritable via *.workspace = true
    std::optional<std::string> default_version;
    std::optional<std::string> default_edition;
    std::optional<std::string> default_license;
    std::optional<std::string> default_repository;
    std::optional<std::string> default_description;

    // [workspace.dependencies]
    std::map<std::string, manifest::DependencySpec> dependencies;

    // [workspace.profile.<name>]
    std::map<std::string, manifest::Profile> profiles;
};

// Walk upward from `start` looking for a Rexy.toml that contains a
// [workspace] table. Returns nullopt if none found.
std::optional<std::filesystem::path>
find_workspace_root(const std::filesystem::path& start);

// Load the workspace root manifest at `manifest_toml`. Throws on parse error
// or invalid schema. The returned manifest's `workspace` field is set; the
// member list is expanded (globs resolved) into absolute paths.
Workspace load(const std::filesystem::path& manifest_toml);

// Apply [workspace.package], [workspace.dependencies], [workspace.profile.*]
// inheritance to a member manifest. Mutates `member` in place. Returns
// diagnostics if a referenced *.workspace = true value is missing from the
// workspace root.
std::vector<diag::Diagnostic>
apply_inheritance(manifest::Manifest& member, const Workspace& ws);

}  // namespace rxy::workspace
