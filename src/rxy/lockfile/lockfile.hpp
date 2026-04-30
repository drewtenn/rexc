#pragma once

#include "manifest/manifest.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rxy::lockfile {

struct LockEntry {
    std::string name;
    std::string version;
    std::string source;                                 // "local" | "git+..." | "registry+..."
    std::optional<std::string> checksum;                // sha256:... — Phase B+
    std::vector<std::string> dependencies;              // sorted "name version" strings
};

struct Lockfile {
    int schema_version = 1;
    std::vector<LockEntry> packages;                    // sorted by (name, version)
};

// Phase A: returns a lockfile with the single root [[package]] entry,
// `source = "local"`, no deps.
Lockfile from_manifest_phase_a(const manifest::Manifest&);

// Read the lockfile at `path`. Returns nullopt if the file does not exist.
// Throws std::runtime_error on parse failure.
std::optional<Lockfile> read(const std::filesystem::path& path);

// Serialize a lockfile to its canonical TOML text.
std::string serialize(const Lockfile&);

void write(const std::filesystem::path&, const Lockfile&);

bool equal(const Lockfile&, const Lockfile&);

}  // namespace rxy::lockfile
