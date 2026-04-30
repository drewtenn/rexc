#include "manifest/manifest.hpp"

#include "util/fs.hpp"

#include <toml++/toml.hpp>

#include <cctype>
#include <set>
#include <sstream>
#include <utility>

namespace rxy::manifest {

namespace fs = std::filesystem;

namespace {

// Allowed top-level keys in Rexy.toml.
const std::set<std::string> kKnownTopLevelKeys = {
    "package", "targets", "profile",
    "dependencies", "dev-dependencies",
    // Reserved for later phases — accepted but ignored, with a warning.
    "build", "workspace", "platform",
};

bool is_valid_package_name(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
        if (!ok) return false;
    }
    if (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_') return false;
    return true;
}

bool is_valid_semver(const std::string& s) {
    if (s.empty()) return false;
    int dots = 0;
    for (char c : s) {
        if (c == '.') ++dots;
        else if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '+') return false;
    }
    return dots >= 2;
}

bool is_valid_edition(const std::string& s) {
    if (s.size() != 4) return false;
    for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

diag::Diagnostic mk_err(const fs::path& f, std::string msg) {
    auto d = diag::Diagnostic::error(std::move(msg));
    d.file = f;
    return d;
}

diag::Diagnostic mk_err_at(const fs::path& f, std::string msg, const toml::source_region& r) {
    auto d = diag::Diagnostic::error(std::move(msg));
    d.file = f;
    if (r.begin.line) {
        d.line = static_cast<int>(r.begin.line);
        d.col  = static_cast<int>(r.begin.column);
    }
    return d;
}

struct LoadCtx {
    const fs::path& manifest_path;
    std::vector<diag::Diagnostic>& errors;
};

std::optional<std::string> require_string(LoadCtx& ctx,
                                           const toml::table& tbl,
                                           const std::string& key,
                                           const std::string& parent_label) {
    auto node = tbl.get(key);
    if (!node) {
        ctx.errors.push_back(mk_err(ctx.manifest_path,
            "missing required field `" + key + "` in " + parent_label));
        return std::nullopt;
    }
    if (!node->is_string()) {
        ctx.errors.push_back(mk_err_at(ctx.manifest_path,
            "field `" + key + "` in " + parent_label + " must be a string",
            node->source()));
        return std::nullopt;
    }
    return std::string{*node->value<std::string>()};
}

std::optional<std::string> opt_string(const toml::table& tbl, const std::string& key) {
    auto node = tbl.get(key);
    if (!node || !node->is_string()) return std::nullopt;
    return std::string{*node->value<std::string>()};
}

std::optional<int> opt_int(const toml::table& tbl, const std::string& key) {
    auto node = tbl.get(key);
    if (!node || !node->is_integer()) return std::nullopt;
    return static_cast<int>(*node->value<int64_t>());
}

std::optional<bool> opt_bool(const toml::table& tbl, const std::string& key) {
    auto node = tbl.get(key);
    if (!node || !node->is_boolean()) return std::nullopt;
    return *node->value<bool>();
}

void parse_profile(LoadCtx& ctx, const toml::table& tbl, Profile& out) {
    out.opt_level = opt_int(tbl, "opt-level");
    out.debug     = opt_bool(tbl, "debug");
    out.lto       = opt_bool(tbl, "lto");
    out.strip     = opt_bool(tbl, "strip");
    if (out.opt_level && (*out.opt_level < 0 || *out.opt_level > 3)) {
        ctx.errors.push_back(mk_err(ctx.manifest_path,
            "profile `opt-level` must be 0, 1, 2, or 3"));
    }
}

}  // namespace

Profile Manifest::resolved_profile(const std::string& name) const {
    Profile defaults;
    if (name == "dev") {
        defaults.opt_level = 0;
        defaults.debug     = true;
        defaults.lto       = false;
        defaults.strip     = false;
    } else if (name == "release") {
        defaults.opt_level = 3;
        defaults.debug     = false;
        defaults.lto       = true;
        defaults.strip     = true;
    }
    auto it = profiles.find(name);
    if (it == profiles.end()) return defaults;
    Profile out = defaults;
    if (it->second.opt_level) out.opt_level = it->second.opt_level;
    if (it->second.debug)     out.debug     = it->second.debug;
    if (it->second.lto)       out.lto       = it->second.lto;
    if (it->second.strip)     out.strip     = it->second.strip;
    return out;
}

std::optional<BinTarget> Manifest::find_bin(const std::string& name) const {
    if (name.empty()) {
        if (bins.size() == 1) return bins.front();
        return std::nullopt;
    }
    for (const auto& b : bins) if (b.name == name) return b;
    return std::nullopt;
}

ManifestResult load_manifest(const fs::path& manifest_toml) {
    std::vector<diag::Diagnostic> errors;
    fs::path abs_path = fs::absolute(manifest_toml);

    if (!fs::is_regular_file(abs_path)) {
        auto d = diag::Diagnostic::error("Rexy.toml not found")
                     .with_help("create one with `rxy init`");
        d.file = abs_path;
        errors.push_back(d);
        return errors;
    }

    std::string text;
    try {
        text = util::read_text_file(abs_path);
    } catch (const std::exception& ex) {
        auto d = diag::Diagnostic::error(ex.what());
        d.file = abs_path;
        errors.push_back(d);
        return errors;
    }

    toml::table tbl;
    try {
        tbl = toml::parse(text, abs_path.string());
    } catch (const toml::parse_error& pe) {
        auto d = diag::Diagnostic::error(std::string("failed to parse manifest: ") + pe.description().data());
        d.file = abs_path;
        if (pe.source().begin.line) {
            d.line = static_cast<int>(pe.source().begin.line);
            d.col  = static_cast<int>(pe.source().begin.column);
        }
        d.help = "Rexy.toml must be valid TOML 1.0 (https://toml.io/)";
        errors.push_back(std::move(d));
        return errors;
    }

    LoadCtx ctx{abs_path, errors};

    // Reject unknown top-level keys (FR-007).
    for (const auto& [k, _v] : tbl) {
        std::string key{k.str()};
        if (kKnownTopLevelKeys.count(key) == 0) {
            ctx.errors.push_back(mk_err(abs_path,
                "unknown top-level key `" + key + "` in Rexy.toml")
                .with_help("known keys: package, targets, profile (Phase A)"));
        }
    }

    Manifest m;
    m.manifest_path = abs_path;
    m.package_root  = abs_path.parent_path();

    // [package]
    auto pkg_node = tbl.get("package");
    bool is_workspace_only = (tbl.get("workspace") != nullptr) && (pkg_node == nullptr);
    if (!is_workspace_only && (!pkg_node || !pkg_node->is_table())) {
        errors.push_back(mk_err(abs_path,
            "missing required `[package]` table"));
    } else if (pkg_node && pkg_node->is_table()) {
        const auto& pkg = *pkg_node->as_table();

        // Helper: read a field that may be either a string OR an inline table
        // with `workspace = true`. Returns (string-value, inherit-flag).
        auto read_inheritable = [&](const std::string& key,
                                      std::string& out_value,
                                      bool& out_inherited) -> bool {
            auto node = pkg.get(key);
            if (!node) return false;
            if (node->is_string()) {
                out_value = *node->value<std::string>();
                return true;
            }
            if (node->is_table()) {
                if (auto ws = (*node->as_table())["workspace"].value<bool>()) {
                    if (*ws) { out_inherited = true; return true; }
                }
                errors.push_back(mk_err_at(abs_path,
                    "field `" + key + "` must be a string or `{ workspace = true }`",
                    node->source()));
                return false;
            }
            errors.push_back(mk_err_at(abs_path,
                "field `" + key + "` must be a string", node->source()));
            return false;
        };

        // name is never inheritable — it's the package's own identity.
        auto name = require_string(ctx, pkg, "name", "[package]");
        if (name) m.package.name = *name;

        // version, edition: required, inheritable.
        bool ok_v = read_inheritable("version", m.package.version, m.package.version_inherited);
        bool ok_e = read_inheritable("edition", m.package.edition, m.package.edition_inherited);
        if (!ok_v && !m.package.version_inherited) {
            errors.push_back(mk_err(abs_path,
                "missing required field `version` in [package]"));
        }
        if (!ok_e && !m.package.edition_inherited) {
            errors.push_back(mk_err(abs_path,
                "missing required field `edition` in [package]"));
        }

        // license, repository, description: optional, inheritable.
        std::string lic, repo, desc;
        bool li=false, ri=false, di=false;
        if (read_inheritable("license", lic, li))    m.package.license    = lic;
        if (read_inheritable("repository", repo, ri)) m.package.repository = repo;
        if (read_inheritable("description", desc, di)) m.package.description = desc;
        m.package.license_inherited     = li;
        m.package.repository_inherited  = ri;
        m.package.description_inherited = di;

        if (name    && !is_valid_package_name(*name))                       errors.push_back(mk_err(abs_path,
            "invalid package name `" + *name + "`")
            .with_help("must start with letter or _, contain only [A-Za-z0-9_-]"));
        if (!m.package.version.empty() && !m.package.version_inherited && !is_valid_semver(m.package.version))
            errors.push_back(mk_err(abs_path,
                "invalid version `" + m.package.version + "`")
                .with_help("expected semver, e.g. \"0.1.0\""));
        if (!m.package.edition.empty() && !m.package.edition_inherited && !is_valid_edition(m.package.edition))
            errors.push_back(mk_err(abs_path,
                "invalid edition `" + m.package.edition + "`")
                .with_help("expected a 4-digit year string, e.g. \"2026\""));
    }

    // [targets]
    auto targets_node = tbl.get("targets");
    if (targets_node) {
        if (!targets_node->is_table()) {
            errors.push_back(mk_err(abs_path, "`targets` must be a table"));
        } else {
            const auto& t = *targets_node->as_table();

            // [targets.lib] — single
            if (auto lib_node = t.get("lib")) {
                if (!lib_node->is_table()) {
                    errors.push_back(mk_err_at(abs_path,
                        "`[targets.lib]` must be a table",
                        lib_node->source()));
                } else {
                    const auto& lib = *lib_node->as_table();
                    auto path_str = opt_string(lib, "path");
                    if (!path_str) {
                        errors.push_back(mk_err(abs_path,
                            "`[targets.lib]` requires a `path` field"));
                    } else {
                        m.lib = LibTarget{fs::path{*path_str}};
                    }
                }
            }

            // [[targets.bin]]
            if (auto bin_node = t.get("bin")) {
                if (!bin_node->is_array()) {
                    errors.push_back(mk_err(abs_path,
                        "`[[targets.bin]]` must be an array of tables"));
                } else {
                    const auto& arr = *bin_node->as_array();
                    for (const auto& el : arr) {
                        if (!el.is_table()) {
                            errors.push_back(mk_err(abs_path,
                                "each `[[targets.bin]]` entry must be a table"));
                            continue;
                        }
                        const auto& b = *el.as_table();
                        auto bn = opt_string(b, "name");
                        auto bp = opt_string(b, "path");
                        if (!bn || !bp) {
                            errors.push_back(mk_err(abs_path,
                                "`[[targets.bin]]` requires `name` and `path`"));
                            continue;
                        }
                        m.bins.push_back(BinTarget{*bn, fs::path{*bp}});
                    }
                }
            }

            // [[targets.test]]
            if (auto test_node = t.get("test")) {
                if (!test_node->is_array()) {
                    errors.push_back(mk_err(abs_path,
                        "`[[targets.test]]` must be an array of tables"));
                } else {
                    const auto& arr = *test_node->as_array();
                    for (const auto& el : arr) {
                        if (!el.is_table()) continue;
                        const auto& b = *el.as_table();
                        auto tn = opt_string(b, "name");
                        auto tp = opt_string(b, "path");
                        if (tn && tp) m.tests.push_back(TestTarget{*tn, fs::path{*tp}});
                    }
                }
            }
        }
    }

    // [dependencies] / [dev-dependencies]
    auto parse_deps_table = [&](const char* table_name,
                                  std::vector<DependencySpec>& out) {
        auto node = tbl.get(table_name);
        if (!node) return;
        if (!node->is_table()) {
            errors.push_back(mk_err(abs_path,
                std::string("`") + table_name + "` must be a table"));
            return;
        }
        for (const auto& [key, val] : *node->as_table()) {
            DependencySpec dep;
            dep.name = std::string{key.str()};

            if (val.is_string()) {
                dep.registry_version = std::string{*val.value<std::string>()};
            } else if (val.is_table()) {
                const auto& t = *val.as_table();
                if (auto ws = t["workspace"].value<bool>()) {
                    if (*ws) {
                        dep.from_workspace = true;
                        out.push_back(std::move(dep));
                        continue;
                    }
                }
                if (auto p = t.get("path")) {
                    if (auto s = p->value<std::string>()) dep.path = std::filesystem::path{*s};
                }
                if (auto g = t.get("git")) {
                    if (auto s = g->value<std::string>()) dep.git_url = *s;
                }
                if (auto r = t.get("rev"))     if (auto s = r->value<std::string>()) dep.git_rev    = *s;
                if (auto r = t.get("tag"))     if (auto s = r->value<std::string>()) dep.git_tag    = *s;
                if (auto r = t.get("branch"))  if (auto s = r->value<std::string>()) dep.git_branch = *s;
                if (auto v = t.get("version")) {
                    if (auto s = v->value<std::string>()) {
                        if (dep.git_url) dep.git_version = *s;
                        else             dep.registry_version = *s;
                    }
                }
                int kinds = (dep.path ? 1 : 0) + (dep.git_url ? 1 : 0) +
                            (dep.registry_version && !dep.git_url ? 1 : 0);
                if (kinds == 0) {
                    errors.push_back(mk_err_at(abs_path,
                        "dependency `" + dep.name + "` must specify one of `path`, `git`, or `version`",
                        val.source()));
                    continue;
                }
                if (kinds > 1) {
                    errors.push_back(mk_err_at(abs_path,
                        "dependency `" + dep.name + "` mixes incompatible source kinds (path / git / version)",
                        val.source()));
                    continue;
                }
            } else {
                errors.push_back(mk_err_at(abs_path,
                    "dependency `" + dep.name + "` must be a string version or an inline table",
                    val.source()));
                continue;
            }

            out.push_back(std::move(dep));
        }
    };
    parse_deps_table("dependencies", m.dependencies);
    parse_deps_table("dev-dependencies", m.dev_dependencies);

    // [build]
    if (auto bnode = tbl.get("build")) {
        if (!bnode->is_table()) {
            errors.push_back(mk_err(abs_path, "`[build]` must be a table"));
        } else {
            const auto& b = *bnode->as_table();
            BuildSection bs;
            if (auto s = opt_string(b, "script")) bs.script = std::filesystem::path{*s};
            if (auto arr = b["allow-scripts"].as_array()) {
                for (const auto& el : *arr) {
                    if (auto v = el.value<std::string>()) bs.allow_scripts.push_back(*v);
                }
            }
            if (auto arr = b["links"].as_array()) {
                for (const auto& el : *arr) {
                    if (auto v = el.value<std::string>()) bs.links.push_back(*v);
                }
            }
            m.build = std::move(bs);
        }
    }

    // [profile.<name>]
    auto profile_node = tbl.get("profile");
    if (profile_node) {
        if (!profile_node->is_table()) {
            errors.push_back(mk_err(abs_path, "`profile` must be a table"));
        } else {
            for (const auto& [name, val] : *profile_node->as_table()) {
                if (!val.is_table()) continue;
                Profile p;
                LoadCtx pctx{abs_path, errors};
                parse_profile(pctx, *val.as_table(), p);
                m.profiles[std::string{name.str()}] = p;
            }
        }
    }

    // Default convention: if no targets declared and src/main.rx exists,
    // synthesize a default bin matching the package name.
    if (!m.lib && m.bins.empty() && !m.package.name.empty()) {
        fs::path default_main = m.package_root / "src" / "main.rx";
        if (fs::is_regular_file(default_main)) {
            m.bins.push_back(BinTarget{m.package.name, fs::path("src/main.rx")});
        } else {
            fs::path default_lib = m.package_root / "src" / "lib.rx";
            if (fs::is_regular_file(default_lib)) {
                m.lib = LibTarget{fs::path("src/lib.rx")};
            }
        }
    }

    if (!errors.empty()) return errors;
    return m;
}

}  // namespace rxy::manifest
