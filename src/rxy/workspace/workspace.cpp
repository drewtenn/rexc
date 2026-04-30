#include "workspace/workspace.hpp"

#include "manifest/manifest.hpp"
#include "util/fs.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

namespace rxy::workspace {

namespace fs = std::filesystem;

namespace {

bool toml_has_workspace(const fs::path& manifest_toml) {
    try {
        std::string text = util::read_text_file(manifest_toml);
        auto tbl = toml::parse(text, manifest_toml.string());
        return tbl.get("workspace") != nullptr;
    } catch (...) {
        return false;
    }
}

// Expand a `members = ["crates/*", "libs/util"]` list into absolute
// directory paths (each must contain a Rexy.toml).
std::vector<fs::path> expand_members(const fs::path& root,
                                       const std::vector<std::string>& patterns) {
    std::vector<fs::path> result;
    for (const auto& pat : patterns) {
        fs::path full = root / pat;
        if (pat.find('*') == std::string::npos) {
            // Literal path
            std::error_code ec;
            if (fs::is_directory(full, ec) && fs::is_regular_file(full / "Rexy.toml", ec)) {
                result.push_back(fs::canonical(full));
            }
            continue;
        }
        // Glob: only support trailing /* patterns (`crates/*`).
        // Substring up to the *.
        auto star = pat.find('*');
        std::string parent_prefix = pat.substr(0, star);
        // Trim trailing '/'.
        while (!parent_prefix.empty() && parent_prefix.back() == '/') parent_prefix.pop_back();
        fs::path parent_dir = root / parent_prefix;
        std::error_code ec;
        if (!fs::is_directory(parent_dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(parent_dir, ec)) {
            if (!entry.is_directory()) continue;
            fs::path m_toml = entry.path() / "Rexy.toml";
            if (fs::is_regular_file(m_toml, ec)) {
                result.push_back(fs::canonical(entry.path()));
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace

std::optional<fs::path> find_workspace_root(const fs::path& start) {
    fs::path dir = fs::absolute(start);
    if (fs::is_regular_file(dir)) dir = dir.parent_path();
    while (true) {
        fs::path candidate = dir / "Rexy.toml";
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec) && toml_has_workspace(candidate)) {
            return dir;
        }
        if (dir == dir.root_path()) return std::nullopt;
        fs::path parent = dir.parent_path();
        if (parent == dir) return std::nullopt;
        dir = parent;
    }
}

Workspace load(const fs::path& manifest_toml) {
    Workspace ws;
    ws.manifest_path = fs::absolute(manifest_toml);
    ws.root          = ws.manifest_path.parent_path();

    std::string text = util::read_text_file(ws.manifest_path);
    auto tbl = toml::parse(text, ws.manifest_path.string());

    auto wsnode = tbl["workspace"].as_table();
    if (!wsnode) {
        throw std::runtime_error("not a workspace root: missing [workspace] in " +
                                  ws.manifest_path.string());
    }

    if (auto rv = (*wsnode)["resolver"].value<std::string>()) {
        if (*rv == "1" || *rv == "3") {
            throw std::runtime_error("rxy v1 supports `resolver = \"2\"` only; got `" + *rv + "`");
        }
        if (*rv != "2") {
            throw std::runtime_error("invalid `resolver = \"" + *rv +
                                      "\"` (must be \"2\")");
        }
        ws.resolver_version = 2;
    }

    std::vector<std::string> member_patterns;
    if (auto arr = (*wsnode)["members"].as_array()) {
        for (const auto& el : *arr) {
            if (auto s = el.value<std::string>()) member_patterns.push_back(*s);
        }
    }
    ws.members = expand_members(ws.root, member_patterns);

    // [workspace.package]
    if (auto wp = (*wsnode)["package"].as_table()) {
        if (auto v = (*wp)["version"].value<std::string>())     ws.default_version     = *v;
        if (auto v = (*wp)["edition"].value<std::string>())     ws.default_edition     = *v;
        if (auto v = (*wp)["license"].value<std::string>())     ws.default_license     = *v;
        if (auto v = (*wp)["repository"].value<std::string>())  ws.default_repository  = *v;
        if (auto v = (*wp)["description"].value<std::string>()) ws.default_description = *v;
    }

    // [workspace.dependencies]
    if (auto wd = (*wsnode)["dependencies"].as_table()) {
        for (const auto& [k, val] : *wd) {
            manifest::DependencySpec d;
            d.name = std::string{k.str()};
            if (val.is_string()) {
                d.registry_version = std::string{*val.value<std::string>()};
            } else if (val.is_table()) {
                const auto& t = *val.as_table();
                if (auto p = t["path"].value<std::string>())   d.path = fs::path{*p};
                if (auto g = t["git"].value<std::string>())    d.git_url = *g;
                if (auto r = t["rev"].value<std::string>())    d.git_rev = *r;
                if (auto r = t["tag"].value<std::string>())    d.git_tag = *r;
                if (auto r = t["branch"].value<std::string>()) d.git_branch = *r;
                if (auto vv = t["version"].value<std::string>()) {
                    if (d.git_url) d.git_version = *vv;
                    else            d.registry_version = *vv;
                }
            }
            ws.dependencies[d.name] = d;
        }
    }

    // [workspace.profile.<name>]
    if (auto wp = (*wsnode)["profile"].as_table()) {
        for (const auto& [name, val] : *wp) {
            if (!val.is_table()) continue;
            const auto& t = *val.as_table();
            manifest::Profile p;
            if (auto v = t["opt-level"].value<int64_t>()) p.opt_level = static_cast<int>(*v);
            if (auto v = t["debug"].value<bool>())        p.debug     = *v;
            if (auto v = t["lto"].value<bool>())          p.lto       = *v;
            if (auto v = t["strip"].value<bool>())        p.strip     = *v;
            ws.profiles[std::string{name.str()}] = p;
        }
    }

    return ws;
}

namespace {
diag::Diagnostic mk_inherit_err(const fs::path& member_manifest,
                                  const fs::path& workspace_manifest,
                                  const std::string& msg) {
    auto d = diag::Diagnostic::error(msg);
    d.file = member_manifest;
    d.notes.push_back("workspace root manifest: " + workspace_manifest.string());
    return d;
}
}  // namespace

std::vector<diag::Diagnostic>
apply_inheritance(manifest::Manifest& m, const Workspace& ws) {
    std::vector<diag::Diagnostic> out;

    if (m.package.version_inherited) {
        if (!ws.default_version) {
            out.push_back(mk_inherit_err(m.manifest_path, ws.manifest_path,
                "package `" + m.package.name +
                "` declares `version.workspace = true` but workspace root has no [workspace.package] version"));
        } else {
            m.package.version = *ws.default_version;
        }
    }
    if (m.package.edition_inherited) {
        if (!ws.default_edition) {
            out.push_back(mk_inherit_err(m.manifest_path, ws.manifest_path,
                "package `" + m.package.name +
                "` declares `edition.workspace = true` but workspace root has no [workspace.package] edition"));
        } else {
            m.package.edition = *ws.default_edition;
        }
    }
    if (m.package.license_inherited && ws.default_license) m.package.license = *ws.default_license;
    if (m.package.repository_inherited && ws.default_repository) m.package.repository = *ws.default_repository;
    if (m.package.description_inherited && ws.default_description) m.package.description = *ws.default_description;

    // Dependencies that opted into workspace inheritance.
    for (auto& d : m.dependencies) {
        if (!d.from_workspace) continue;
        auto it = ws.dependencies.find(d.name);
        if (it == ws.dependencies.end()) {
            out.push_back(mk_inherit_err(m.manifest_path, ws.manifest_path,
                "dependency `" + d.name +
                ".workspace = true` but workspace root [workspace.dependencies] has no entry for `" +
                d.name + "`"));
            continue;
        }
        const auto& src = it->second;
        // Inherit version + source; keep the dep's name.
        d.path             = src.path;
        d.git_url          = src.git_url;
        d.git_rev          = src.git_rev;
        d.git_tag          = src.git_tag;
        d.git_branch       = src.git_branch;
        d.git_version      = src.git_version;
        d.registry_version = src.registry_version;
        // path entries from the workspace are relative to the workspace root,
        // not the member. Resolve to absolute now to avoid surprises later.
        if (d.path && d.path->is_relative()) {
            d.path = (ws.root / *d.path).lexically_normal();
        }
    }

    // Profile inheritance: workspace profile is the BASE; member's profile
    // overrides per-key (shadow merge).
    for (const auto& [name, ws_prof] : ws.profiles) {
        manifest::Profile base = ws_prof;
        auto it = m.profiles.find(name);
        if (it != m.profiles.end()) {
            const auto& mp = it->second;
            if (mp.opt_level) base.opt_level = mp.opt_level;
            if (mp.debug)     base.debug     = mp.debug;
            if (mp.lto)       base.lto       = mp.lto;
            if (mp.strip)     base.strip     = mp.strip;
        }
        m.profiles[name] = base;
    }

    return out;
}

}  // namespace rxy::workspace
