#include "lockfile/lockfile.hpp"

#include "util/fs.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace rxy::lockfile {

namespace fs = std::filesystem;

namespace {
void sort_in_canonical_order(Lockfile& l) {
    std::sort(l.packages.begin(), l.packages.end(),
              [](const LockEntry& a, const LockEntry& b) {
                  if (a.name != b.name) return a.name < b.name;
                  return a.version < b.version;
              });
    for (auto& p : l.packages) {
        std::sort(p.dependencies.begin(), p.dependencies.end());
    }
}
}  // namespace

Lockfile from_manifest_phase_a(const manifest::Manifest& m) {
    Lockfile l;
    l.schema_version = 1;
    LockEntry root;
    root.name    = m.package.name;
    root.version = m.package.version;
    root.source  = "local";
    l.packages.push_back(std::move(root));
    sort_in_canonical_order(l);
    return l;
}

Lockfile from_resolution(const manifest::Manifest& root,
                          const resolver::Resolution& res) {
    Lockfile l;
    l.schema_version = 1;

    LockEntry root_entry;
    root_entry.name    = root.package.name;
    root_entry.version = root.package.version;
    root_entry.source  = "local";
    {
        auto it = res.dep_edges.find(root.package.name);
        if (it != res.dep_edges.end()) {
            for (const auto& sub_name : it->second) {
                for (const auto& p : res.packages) {
                    if (p.name == sub_name) {
                        root_entry.dependencies.push_back(p.name + " " + p.version);
                        break;
                    }
                }
            }
        }
    }
    l.packages.push_back(std::move(root_entry));

    for (const auto& r : res.packages) {
        LockEntry e;
        e.name    = r.name;
        e.version = r.version;
        e.source  = r.source_token;
        e.commit  = r.commit;
        e.checksum = r.checksum;
        auto it = res.dep_edges.find(r.name);
        if (it != res.dep_edges.end()) {
            for (const auto& sub_name : it->second) {
                for (const auto& p : res.packages) {
                    if (p.name == sub_name) {
                        e.dependencies.push_back(p.name + " " + p.version);
                        break;
                    }
                }
            }
        }
        l.packages.push_back(std::move(e));
    }

    sort_in_canonical_order(l);
    return l;
}

std::optional<std::string> detect_drift(const Lockfile& previous,
                                         const Lockfile& fresh) {
    auto find = [](const Lockfile& l, const std::string& name) -> const LockEntry* {
        for (const auto& e : l.packages) if (e.name == name) return &e;
        return nullptr;
    };
    for (const auto& f : fresh.packages) {
        const LockEntry* old = find(previous, f.name);
        if (!old) continue;
        if (f.source.rfind("git+", 0) != 0) continue;  // only git sources drift in this sense
        if (old->commit && f.commit && *old->commit != *f.commit) {
            return std::string("lockfile pinned `") + f.name + " " + f.version +
                "` at commit " + *old->commit + ", but the source now resolves to commit " +
                *f.commit + ". This usually means a tag was force-pushed upstream.";
        }
        if (old->checksum && f.checksum && *old->checksum != *f.checksum) {
            return std::string("lockfile pinned `") + f.name + " " + f.version +
                "` with checksum " + *old->checksum +
                ", but the source now hashes to " + *f.checksum + ".";
        }
    }
    return std::nullopt;
}

std::optional<Lockfile> read(const fs::path& path) {
    if (!fs::is_regular_file(path)) return std::nullopt;
    std::string text = util::read_text_file(path);
    toml::table tbl;
    try {
        tbl = toml::parse(text, path.string());
    } catch (const toml::parse_error& pe) {
        std::ostringstream oss;
        oss << "lockfile parse error at " << path.string()
            << ":" << pe.source().begin.line << ":" << pe.source().begin.column
            << " — " << pe.description();
        throw std::runtime_error(oss.str());
    }

    Lockfile l;
    if (auto v = tbl["version"].value<int64_t>()) l.schema_version = static_cast<int>(*v);
    if (l.schema_version != 1) {
        throw std::runtime_error("unsupported lockfile schema version " +
                                  std::to_string(l.schema_version));
    }

    auto pkgs = tbl["package"].as_array();
    if (pkgs) {
        for (const auto& el : *pkgs) {
            if (!el.is_table()) continue;
            const auto& t = *el.as_table();
            LockEntry e;
            if (auto n = t["name"].value<std::string>())     e.name    = *n;
            if (auto v = t["version"].value<std::string>())  e.version = *v;
            if (auto s = t["source"].value<std::string>())   e.source  = *s;
            if (auto c = t["commit"].value<std::string>())   e.commit   = *c;
            if (auto c = t["checksum"].value<std::string>()) e.checksum = *c;
            if (auto deps = t["dependencies"].as_array()) {
                for (const auto& d : *deps) {
                    if (auto s = d.value<std::string>()) e.dependencies.push_back(*s);
                }
            }
            l.packages.push_back(std::move(e));
        }
    }
    sort_in_canonical_order(l);
    return l;
}

std::string serialize(const Lockfile& in) {
    Lockfile l = in;
    sort_in_canonical_order(l);

    std::ostringstream oss;
    oss << "# Rexy.lock — DO NOT EDIT BY HAND.\n";
    oss << "# Generated by rxy. Sorted alphabetically by (name, version).\n";
    oss << "version = " << l.schema_version << "\n";

    for (const auto& p : l.packages) {
        oss << "\n[[package]]\n";
        oss << "name = \""    << p.name    << "\"\n";
        oss << "version = \"" << p.version << "\"\n";
        oss << "source = \""  << p.source  << "\"\n";
        if (p.commit) {
            oss << "commit = \"" << *p.commit << "\"\n";
        }
        if (p.checksum) {
            oss << "checksum = \"" << *p.checksum << "\"\n";
        }
        if (!p.dependencies.empty()) {
            oss << "dependencies = [\n";
            for (const auto& d : p.dependencies) {
                oss << "  \"" << d << "\",\n";
            }
            oss << "]\n";
        }
    }
    return oss.str();
}

void write(const fs::path& path, const Lockfile& l) {
    util::atomic_write_text_file(path, serialize(l));
}

bool equal(const Lockfile& a, const Lockfile& b) {
    return serialize(a) == serialize(b);
}

}  // namespace rxy::lockfile
