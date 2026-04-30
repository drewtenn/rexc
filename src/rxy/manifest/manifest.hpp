#pragma once

#include "diag/diag.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rxy::manifest {

struct PackageMeta {
    std::string name;
    std::string version;             // semver string, e.g. "0.1.0"
    std::string edition;             // year string, e.g. "2026"
    std::optional<std::string> description;
    std::optional<std::string> license;
    std::optional<std::string> repository;
};

struct LibTarget {
    std::filesystem::path path;      // relative to package_root
};

struct BinTarget {
    std::string name;
    std::filesystem::path path;      // relative to package_root
};

struct TestTarget {
    std::string name;
    std::filesystem::path path;
};

struct Profile {
    std::optional<int> opt_level;    // 0..3
    std::optional<bool> debug;
    std::optional<bool> lto;
    std::optional<bool> strip;
};

// One [dependencies] entry. Phase B supports three shapes:
//   foo = "0.4"                                  → registry (errors in Phase B)
//   foo = { path = "../foo" }                    → path source
//   foo = { git = "...", tag/rev/branch = ... }  → git source
struct DependencySpec {
    std::string name;                              // the import handle / lookup key

    // exactly one source kind is set
    std::optional<std::string> registry_version;   // "0.4" — errors before Phase C
    std::optional<std::filesystem::path> path;     // relative or absolute filesystem path
    std::optional<std::string> git_url;
    std::optional<std::string> git_tag;
    std::optional<std::string> git_rev;
    std::optional<std::string> git_branch;
    std::optional<std::string> git_version;        // optional semver constraint with git source

    bool is_path()     const { return path.has_value(); }
    bool is_git()      const { return git_url.has_value(); }
    bool is_registry() const { return !is_path() && !is_git() && registry_version.has_value(); }
};

struct Manifest {
    PackageMeta package;
    std::optional<LibTarget> lib;
    std::vector<BinTarget> bins;
    std::vector<TestTarget> tests;
    std::map<std::string, Profile> profiles;   // "dev", "release", custom
    std::vector<DependencySpec> dependencies;  // [dependencies]
    std::vector<DependencySpec> dev_dependencies;  // [dev-dependencies] — parsed but currently unused

    std::filesystem::path manifest_path;       // absolute path to Rexy.toml
    std::filesystem::path package_root;        // dir containing Rexy.toml

    // Resolves the effective profile (built-in defaults for "dev"/"release"
    // unless the manifest overrode them).
    Profile resolved_profile(const std::string& name) const;

    // Convenience: returns the binary target named `name`, or the sole bin
    // if `name` is empty and there's exactly one. Returns std::nullopt if
    // ambiguous or not found.
    std::optional<BinTarget> find_bin(const std::string& name) const;
};

using ManifestResult = std::variant<Manifest, std::vector<diag::Diagnostic>>;

ManifestResult load_manifest(const std::filesystem::path& manifest_toml);

}  // namespace rxy::manifest
