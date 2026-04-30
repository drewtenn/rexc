#include "registry/registry.hpp"

#include "cache/cache.hpp"
#include "process/process.hpp"
#include "util/fs.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rxy::registry {

namespace fs = std::filesystem;

namespace {

fs::path home_config_path() {
    return cache::cache_root() / "config.toml";
}

fs::path registry_root_for(const std::string& name) {
    return cache::cache_root() / "registries" / name / "index";
}

void write_default_config_if_missing() {
    fs::path cfg = home_config_path();
    if (fs::exists(cfg)) return;
    std::ostringstream oss;
    oss << "# rxy global config\n";
    oss << "[registries.default]\n";
    oss << "# Default to a local empty index — users override this in their\n"
        << "# config to point at the public Rexy registry once Phase C ships.\n";
    oss << "url    = \"\"\n";
    oss << "local  = true\n";
    oss << "refresh-ttl-seconds = 600\n";
    util::atomic_write_text_file(cfg, oss.str());
}

RegistryConfig load_config(const std::string& name) {
    write_default_config_if_missing();
    RegistryConfig rc;
    rc.name = name;
    rc.index_root = registry_root_for(name);

    fs::path cfg = home_config_path();
    if (fs::exists(cfg)) {
        std::string text = util::read_text_file(cfg);
        try {
            auto tbl = toml::parse(text, cfg.string());
            auto regs = tbl["registries"];
            if (auto t = regs[name].as_table()) {
                if (auto u = (*t)["url"].value<std::string>()) {
                    if (!u->empty()) rc.git_url = *u;
                }
                if (auto ttl = (*t)["refresh-ttl-seconds"].value<int64_t>()) {
                    rc.refresh_ttl_seconds = static_cast<int>(*ttl);
                }
            }
        } catch (const toml::parse_error& pe) {
            throw std::runtime_error("could not parse rxy config at " + cfg.string() +
                ": " + pe.description().data());
        }
    }
    cache::ensure_dir(rc.index_root);
    // Ensure packages/ exists for empty/local registries.
    cache::ensure_dir(rc.index_root / "packages");
    return rc;
}

bool has_conflict_markers(const fs::path& dir) {
    // Cheap probe: scan a couple of known files for `<<<<<<<` headers.
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const auto& f : {fs::path("config.toml")}) {
        fs::path p = dir / f;
        if (!fs::is_regular_file(p, ec)) continue;
        std::ifstream in(p);
        std::string line;
        while (std::getline(in, line)) {
            if (line.find("<<<<<<<") == 0) return true;
        }
    }
    return false;
}

}  // namespace

std::string normalize_name(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == '_') out.push_back('-');
        else          out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string bucket_prefix(const std::string& n) {
    if (n.empty()) return "0";
    if (n.size() == 1) return std::string("1/") + n;
    if (n.size() == 2) return std::string("2/") + n;
    if (n.size() == 3) return std::string("3/") + n.substr(0, 1) + "/" + n.substr(1);
    return n.substr(0, 2) + "/" + n.substr(2, 2);
}

fs::path Registry::package_file(const std::string& name) const {
    std::string n = normalize_name(name);
    return config.index_root / "packages" / bucket_prefix(n) / (n + ".toml");
}

std::optional<PackageInfo> Registry::lookup(const std::string& name) const {
    fs::path p = package_file(name);
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return std::nullopt;
    std::string text = util::read_text_file(p);
    toml::table tbl;
    try {
        tbl = toml::parse(text, p.string());
    } catch (const toml::parse_error& pe) {
        throw std::runtime_error("malformed registry entry " + p.string() + ": " + pe.description().data());
    }

    PackageInfo info;
    if (auto n = tbl["name"].value<std::string>()) info.name = *n;
    if (info.name.empty()) info.name = normalize_name(name);

    auto entries = tbl["entries"].as_array();
    if (!entries) return info;
    for (const auto& el : *entries) {
        if (!el.is_table()) continue;
        const auto& t = *el.as_table();
        Entry e;
        if (auto v = t["version"].value<std::string>()) {
            auto parsed = semver::parse_version(*v);
            if (!parsed) continue;
            e.version = *parsed;
        }
        if (auto u = t["git-url"].value<std::string>())  e.git_url  = *u;
        if (auto c = t["commit"].value<std::string>())   e.commit   = *c;
        if (auto c = t["checksum"].value<std::string>()) e.checksum = *c;
        if (auto y = t["yanked"].value<bool>())          e.yanked   = *y;
        if (auto sev = t["yank-severity"].value<std::string>()) {
            e.yank_severity = (*sev == "security") ? YankSeverity::Security
                                                    : YankSeverity::Informational;
        }
        if (auto r = t["yank-reason"].value<std::string>()) e.yank_reason = *r;
        if (auto p2 = t["published-at"].value<std::string>()) e.published_at = *p2;
        if (auto deps_arr = t["deps"].as_array()) {
            for (const auto& d : *deps_arr) {
                if (auto s = d.value<std::string>()) e.deps.push_back(*s);
            }
        }
        info.entries.push_back(std::move(e));
    }
    return info;
}

void Registry::append_entry(const std::string& name, const Entry& entry) {
    fs::path file = package_file(name);
    cache::ensure_dir(file.parent_path());

    PackageInfo info;
    if (auto loaded = lookup(name)) info = *loaded;
    if (info.name.empty()) info.name = normalize_name(name);

    for (const auto& existing : info.entries) {
        if (existing.version == entry.version) {
            throw std::runtime_error("version " + entry.version.to_string() +
                " already published for `" + info.name + "`");
        }
    }
    info.entries.push_back(entry);

    // Re-serialize.
    std::ostringstream oss;
    oss << "name = \"" << info.name << "\"\n";
    for (const auto& e : info.entries) {
        oss << "\n[[entries]]\n";
        oss << "version = \"" << e.version.to_string() << "\"\n";
        oss << "git-url = \"" << e.git_url << "\"\n";
        oss << "commit  = \"" << e.commit  << "\"\n";
        oss << "checksum = \"" << e.checksum << "\"\n";
        if (!e.deps.empty()) {
            oss << "deps = [\n";
            for (const auto& d : e.deps) oss << "  \"" << d << "\",\n";
            oss << "]\n";
        }
        oss << "yanked = " << (e.yanked ? "true" : "false") << "\n";
        if (e.yanked) {
            oss << "yank-severity = \""
                << (e.yank_severity == YankSeverity::Security ? "security" : "informational")
                << "\"\n";
            if (e.yank_reason) oss << "yank-reason = \"" << *e.yank_reason << "\"\n";
        }
        if (!e.published_at.empty()) oss << "published-at = \"" << e.published_at << "\"\n";
    }
    util::atomic_write_text_file(file, oss.str());
}

void Registry::set_yanked(const std::string& name,
                           const semver::Version& v,
                           bool yanked,
                           YankSeverity severity,
                           std::optional<std::string> reason) {
    fs::path file = package_file(name);
    if (!fs::exists(file)) {
        throw std::runtime_error("no registry entry for `" + name + "`");
    }
    PackageInfo info = lookup(name).value();
    bool found = false;
    for (auto& e : info.entries) {
        if (e.version == v) {
            e.yanked = yanked;
            e.yank_severity = severity;
            e.yank_reason = std::move(reason);
            found = true;
            break;
        }
    }
    if (!found) {
        throw std::runtime_error("no entry " + name + " " + v.to_string() + " in registry");
    }

    // Re-serialize directly (similar to append, but no append).
    std::ostringstream oss;
    oss << "name = \"" << info.name << "\"\n";
    for (const auto& e : info.entries) {
        oss << "\n[[entries]]\n";
        oss << "version = \"" << e.version.to_string() << "\"\n";
        oss << "git-url = \"" << e.git_url << "\"\n";
        oss << "commit  = \"" << e.commit  << "\"\n";
        oss << "checksum = \"" << e.checksum << "\"\n";
        if (!e.deps.empty()) {
            oss << "deps = [\n";
            for (const auto& d : e.deps) oss << "  \"" << d << "\",\n";
            oss << "]\n";
        }
        oss << "yanked = " << (e.yanked ? "true" : "false") << "\n";
        if (e.yanked) {
            oss << "yank-severity = \""
                << (e.yank_severity == YankSeverity::Security ? "security" : "informational")
                << "\"\n";
            if (e.yank_reason) oss << "yank-reason = \"" << *e.yank_reason << "\"\n";
        }
        if (!e.published_at.empty()) oss << "published-at = \"" << e.published_at << "\"\n";
    }
    util::atomic_write_text_file(file, oss.str());
}

Registry open_default() { return open_named("default"); }

Registry open_named(const std::string& name) {
    Registry r;
    r.config = load_config(name);
    return r;
}

void refresh_if_stale(Registry& r, bool force) {
    // Local-FS registry: no-op.
    if (!r.config.git_url) return;

    fs::path stamp = r.config.index_root / ".last-fetch";
    bool stale = force;
    if (!stale) {
        std::error_code ec;
        auto when = fs::last_write_time(stamp, ec);
        if (ec) {
            stale = true;
        } else {
            // C++17: convert file_clock → system_clock by re-stat'ing as seconds-since-epoch.
            auto file_age = std::chrono::duration_cast<std::chrono::seconds>(
                fs::file_time_type::clock::now() - when).count();
            if (file_age > r.config.refresh_ttl_seconds) stale = true;
        }
    }
    if (!stale) return;

    // Self-heal if the index is corrupt.
    if (has_conflict_markers(r.config.index_root)) {
        std::error_code ec;
        fs::remove_all(r.config.index_root, ec);
        cache::ensure_dir(r.config.index_root);
    }

    // First-time clone or refresh.
    fs::path git_marker = r.config.index_root / ".git";
    if (!fs::exists(git_marker)) {
        // Clone fresh.
        std::error_code ec;
        fs::remove_all(r.config.index_root, ec);
        cache::ensure_dir(r.config.index_root.parent_path());
        process::Options popts;
        popts.cwd = r.config.index_root.parent_path();
        popts.stream_through = false;
        process::Result pr = process::run("/usr/bin/env",
            {"git", "clone", "--quiet", *r.config.git_url, r.config.index_root.string()},
            popts);
        if (pr.exit_code != 0) {
            throw std::runtime_error("git clone of registry index failed: " + pr.stderr_data);
        }
    } else {
        process::Options popts;
        popts.cwd = r.config.index_root;
        popts.stream_through = false;
        process::Result pr = process::run("/usr/bin/env",
            {"git", "pull", "--quiet", "--ff-only", "--tags", "--force"},
            popts);
        if (pr.exit_code != 0) {
            throw std::runtime_error("git pull of registry index failed: " + pr.stderr_data);
        }
    }
    std::ofstream(stamp) << "ok\n";
}

}  // namespace rxy::registry
