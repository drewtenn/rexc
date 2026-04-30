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

struct Manifest {
    PackageMeta package;
    std::optional<LibTarget> lib;
    std::vector<BinTarget> bins;
    std::vector<TestTarget> tests;
    std::map<std::string, Profile> profiles;   // "dev", "release", custom

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
