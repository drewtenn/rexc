#pragma once

#include "semver/semver.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rxy::registry {

enum class YankSeverity { Informational, Security };

struct Entry {
    semver::Version version;
    std::string git_url;            // for git-backed packages
    std::string commit;             // immutable commit SHA
    std::string checksum;           // sha256:<hex> of the source tree
    std::vector<std::string> deps;  // raw "name <constraint>" strings
    bool yanked = false;
    YankSeverity yank_severity = YankSeverity::Informational;
    std::optional<std::string> yank_reason;
    std::string published_at;       // RFC 3339 timestamp
};

struct PackageInfo {
    std::string name;
    std::vector<Entry> entries;     // chronological order; newest last
};

struct RegistryConfig {
    std::string name;               // e.g. "default"
    std::filesystem::path index_root; // local clone of the index repo
    std::optional<std::string> git_url; // nullopt for local-FS registries
    int refresh_ttl_seconds = 600;
};

// Per-process registry handle.
struct Registry {
    RegistryConfig config;

    // Look up a package's index file. Returns nullopt if not present.
    std::optional<PackageInfo> lookup(const std::string& name) const;

    // Append a new entry. Validates: version must not already exist.
    // Caller is responsible for filling git_url, commit, checksum, deps.
    void append_entry(const std::string& name, const Entry& entry);

    // Toggle a yank flag on an existing entry. Throws if the entry doesn't exist.
    void set_yanked(const std::string& name,
                     const semver::Version&,
                     bool yanked,
                     YankSeverity severity = YankSeverity::Informational,
                     std::optional<std::string> reason = std::nullopt);

    // Path on disk for a package's index file.
    std::filesystem::path package_file(const std::string& name) const;
};

// Load the registry config from $REXY_HOME/config.toml. Creates a default
// "default" registry on first use if no config exists. Test code can pass
// REXY_HOME via env to redirect.
Registry open_default();
Registry open_named(const std::string& name);

// Refresh (git pull) the index repo if older than refresh_ttl_seconds.
// No-op for local-FS registries. Triggers self-heal (delete + reclone) if
// the local clone is in a corrupt state.
void refresh_if_stale(Registry&, bool force = false);

// Make `name` lowercase + replace `_` with `-` for normalization.
std::string normalize_name(const std::string& name);

// 2-letter bucket prefix for the index path. Special-case <=2 char names.
std::string bucket_prefix(const std::string& normalized_name);

}  // namespace rxy::registry
