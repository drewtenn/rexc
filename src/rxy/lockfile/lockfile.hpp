#pragma once

#include "manifest/manifest.hpp"
#include "resolver/resolver.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rxy::lockfile {

struct LockEntry {
    std::string name;
    std::string version;
    std::string source;                                 // "local" | "path+..." | "git+..." | "registry+..."
    std::optional<std::string> commit;                  // git SHA-1 hex, git sources only
    std::optional<std::string> checksum;                // sha256:... — git/registry sources
    std::vector<std::string> dependencies;              // sorted "name version" strings
};

struct Lockfile {
    int schema_version = 1;
    std::vector<LockEntry> packages;                    // sorted by (name, version)
};

// Phase A: returns a lockfile with the single root [[package]] entry,
// `source = "local"`, no deps.
Lockfile from_manifest_phase_a(const manifest::Manifest&);

// Phase B: build the lockfile from the root manifest + resolution graph.
Lockfile from_resolution(const manifest::Manifest& root,
                          const resolver::Resolution& resolution);

// Read the lockfile at `path`. Returns nullopt if the file does not exist.
// Throws std::runtime_error on parse failure.
std::optional<Lockfile> read(const std::filesystem::path& path);

// Serialize a lockfile to its canonical TOML text.
std::string serialize(const Lockfile&);

void write(const std::filesystem::path&, const Lockfile&);

bool equal(const Lockfile&, const Lockfile&);

// Phase B drift detection: returns a non-empty diagnostic if the previously
// locked entry for `name` exists and differs from the freshly resolved one
// (different commit OR different checksum). Path/local entries are ignored.
std::optional<std::string> detect_drift(const Lockfile& previous,
                                         const Lockfile& fresh);

}  // namespace rxy::lockfile
