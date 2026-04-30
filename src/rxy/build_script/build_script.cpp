#include "build_script/build_script.hpp"

#include "diag/diag.hpp"
#include "hash/sha256.hpp"
#include "process/process.hpp"
#include "util/fs.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace rxy::build_script {

namespace fs = std::filesystem;

// ----------------------------------------------------------------------------
// Directive parsing
// ----------------------------------------------------------------------------

Directives parse_stdout(const std::string& text) {
    Directives d;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        // Strip trailing \r
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("rxy:", 0) != 0) continue;
        std::string body = line.substr(4);
        auto eq = body.find('=');
        if (eq == std::string::npos) continue;
        std::string key = body.substr(0, eq);
        std::string val = body.substr(eq + 1);
        if      (key == "rerun-if-changed")     d.rerun_if_changed.emplace_back(val);
        else if (key == "rerun-if-env-changed") d.rerun_if_env_changed.push_back(val);
        else if (key == "rxy-search-path")      d.rxy_search_paths.emplace_back(val);
        else if (key == "warning")              d.warnings.push_back(val);
        else if (key == "cfg")                  d.cfgs.push_back(val);
        else if (key == "link-lib")             d.link_libs.push_back(val);
        else if (key == "link-search")          d.link_searches.push_back(val);
        else if (key == "env") {
            auto e = val.find('=');
            if (e != std::string::npos) {
                d.env_overlay[val.substr(0, e)] = val.substr(e + 1);
            }
        }
    }
    return d;
}

// ----------------------------------------------------------------------------
// Cache hash
// ----------------------------------------------------------------------------

std::string cache_hash(const manifest::Manifest& m, const RunOptions& opts,
                        const std::string& script_src_text) {
    std::string buf;
    buf.reserve(256 + script_src_text.size());
    buf += m.package.name;       buf += '\0';
    buf += m.package.version;    buf += '\0';
    buf += opts.profile_name;    buf += '\0';
    buf += opts.rexc_version;    buf += '\0';
    buf += opts.host_triple;     buf += '\0';
    buf += opts.target_triple;   buf += '\0';
    buf += script_src_text;
    std::string h = hash::sha256_hex(buf);
    if (h.rfind("sha256:", 0) == 0) h = h.substr(7);
    return h.substr(0, 16);          // first 16 hex chars: plenty for cache keys
}

// ----------------------------------------------------------------------------
// Mtime helpers
// ----------------------------------------------------------------------------

namespace {

fs::file_time_type mtime_or_zero(const fs::path& p) {
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    if (ec) return fs::file_time_type{};
    return t;
}

bool any_source_newer(const fs::path& root, fs::file_time_type cutoff) {
    static const std::set<std::string> kSkipDirs = {"target", ".git"};
    static const std::set<std::string> kSkipFiles = {"Rexy.lock", ".rxy_extracted"};
    std::error_code ec;
    if (!fs::exists(root, ec)) return false;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        const fs::path& p = it->path();
        std::string name = p.filename().string();
        if (it->is_directory()) {
            if (kSkipDirs.count(name)) it.disable_recursion_pending();
            continue;
        }
        if (kSkipFiles.count(name)) continue;
        auto t = mtime_or_zero(p);
        if (t > cutoff) return true;
    }
    return false;
}

void snapshot_source_mtimes(const fs::path& root,
                              std::map<std::string, fs::file_time_type>& out) {
    static const std::set<std::string> kSkipDirs = {"target", ".git"};
    static const std::set<std::string> kSkipFiles = {"Rexy.lock", ".rxy_extracted"};
    std::error_code ec;
    if (!fs::exists(root, ec)) return;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        const fs::path& p = it->path();
        std::string name = p.filename().string();
        if (it->is_directory()) {
            if (kSkipDirs.count(name)) it.disable_recursion_pending();
            continue;
        }
        if (kSkipFiles.count(name)) continue;
        out[fs::relative(p, root).generic_string()] = mtime_or_zero(p);
    }
}

void detect_source_mutation(const fs::path& root,
                              const std::map<std::string, fs::file_time_type>& before) {
    std::map<std::string, fs::file_time_type> after;
    snapshot_source_mtimes(root, after);
    for (const auto& [path, t] : after) {
        auto it = before.find(path);
        if (it == before.end()) {
            throw std::runtime_error("build script created file outside OUT_DIR: " + path +
                " — build scripts must only write under $OUT_DIR (FR-025)");
        }
        if (it->second != t) {
            throw std::runtime_error("build script modified package source: " + path +
                " — build scripts must only write under $OUT_DIR (FR-025)");
        }
    }
    for (const auto& [path, t] : before) {
        if (after.find(path) == after.end()) {
            throw std::runtime_error("build script deleted package source: " + path +
                " — build scripts must only write under $OUT_DIR (FR-025)");
        }
    }
}

void persist_invoked(const fs::path& invoked_toml,
                      const Directives& d,
                      const std::map<std::string, std::string>& env_at_run) {
    std::ostringstream oss;
    oss << "# rxy build-script cache marker — DO NOT EDIT BY HAND\n";
    if (!d.rerun_if_changed.empty()) {
        oss << "rerun-if-changed = [\n";
        for (const auto& p : d.rerun_if_changed) oss << "  \"" << p.string() << "\",\n";
        oss << "]\n";
    }
    if (!d.rerun_if_env_changed.empty()) {
        oss << "rerun-if-env-changed = [\n";
        for (const auto& v : d.rerun_if_env_changed) oss << "  \"" << v << "\",\n";
        oss << "]\n";
        oss << "[env-at-run]\n";
        for (const auto& v : d.rerun_if_env_changed) {
            auto it = env_at_run.find(v);
            std::string val = (it == env_at_run.end()) ? "" : it->second;
            oss << v << " = \"" << val << "\"\n";
        }
    }
    util::atomic_write_text_file(invoked_toml, oss.str());
}

bool needs_rerun(const fs::path& cache_dir,
                  const fs::path& package_root,
                  const fs::path& script_src) {
    fs::path output     = cache_dir / "output";
    fs::path invoked    = cache_dir / "invoked.toml";
    if (!fs::exists(output)) return true;
    auto cutoff = mtime_or_zero(output);
    if (mtime_or_zero(script_src) > cutoff) return true;

    // env vars
    if (fs::exists(invoked)) {
        std::string text = util::read_text_file(invoked);
        // very small parser: look for [env-at-run] and "VAR = "VAL""
        auto pos = text.find("[env-at-run]");
        if (pos != std::string::npos) {
            std::istringstream in(text.substr(pos));
            std::string line;
            std::getline(in, line);   // skip header
            while (std::getline(in, line)) {
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string var = line.substr(0, eq);
                while (!var.empty() && (var.back() == ' ' || var.back() == '\t')) var.pop_back();
                std::string after = line.substr(eq + 1);
                size_t a = after.find('"'), b = after.rfind('"');
                if (a == std::string::npos || b == std::string::npos || b <= a) continue;
                std::string val = after.substr(a + 1, b - a - 1);
                const char* live = std::getenv(var.c_str());
                std::string live_str = live ? live : "";
                if (live_str != val) return true;
            }
        }

        auto rrcs = text.find("rerun-if-changed = [");
        if (rrcs != std::string::npos) {
            // each entry like   "PATH",
            std::string sub = text.substr(rrcs);
            size_t i = 0;
            bool found_any = false;
            while ((i = sub.find('"', i)) != std::string::npos) {
                size_t j = sub.find('"', i + 1);
                if (j == std::string::npos) break;
                std::string p = sub.substr(i + 1, j - i - 1);
                fs::path abs = package_root / p;
                if (mtime_or_zero(abs) > cutoff) return true;
                i = j + 1;
                found_any = true;
            }
            if (found_any) return false;
        }
    }

    // Default: any source-tree change re-runs (Cargo's policy).
    if (any_source_newer(package_root, cutoff)) return true;
    return false;
}

}  // namespace

// ----------------------------------------------------------------------------
// run_for — top-level dispatcher
// ----------------------------------------------------------------------------

Directives run_for(const manifest::Manifest& m, const RunOptions& opts) {
    Directives empty;
    if (opts.no_build_scripts) return empty;
    if (!m.build || !m.build->script) return empty;

    fs::path script_src = m.package_root / *m.build->script;
    if (!fs::is_regular_file(script_src)) {
        throw std::runtime_error("build.script `" + m.build->script->string() +
            "` does not exist at " + script_src.string());
    }

    std::string script_text = util::read_text_file(script_src);
    std::string h = cache_hash(m, opts, script_text);
    fs::path cache_dir = opts.target_dir_root / "build" / (m.package.name + "-" + h);
    fs::create_directories(cache_dir);
    fs::path out_dir = cache_dir / "out";
    fs::create_directories(out_dir);
    fs::path script_bin = cache_dir / "build_script";

    bool rerun = needs_rerun(cache_dir, m.package_root, script_src);

    if (!rerun) {
        std::string cached_stdout = util::read_text_file(cache_dir / "output");
        return parse_stdout(cached_stdout);
    }

    if (!opts.quiet) {
        diag::status("Compiling", m.package.name + " (build script)");
    }

    // 1) Compile build.rx → cache_dir/build_script
    {
        process::Options popts;
        popts.cwd = m.package_root;
        popts.stream_through = !opts.quiet;
        std::vector<std::string> args = {script_src.string(), "-o", script_bin.string()};
        process::Result pr = process::run(opts.rexc_exe, args, popts);
        if (pr.exit_code != 0) {
            throw std::runtime_error("build script `" + m.build->script->string() +
                "` failed to compile (rexc exit " + std::to_string(pr.exit_code) + ")");
        }
    }

    if (!opts.quiet) {
        diag::status("Running", m.package.name + " (build script)");
    }

    // 2) Snapshot source-tree mtimes BEFORE running for FR-025 enforcement.
    std::map<std::string, fs::file_time_type> before_mtimes;
    snapshot_source_mtimes(m.package_root, before_mtimes);

    // 3) Run the script with OUT_DIR set, capture stdout/stderr.
    process::Options popts;
    popts.cwd = m.package_root;
    popts.stream_through = false;
    popts.env_overlay["OUT_DIR"]            = out_dir.string();
    popts.env_overlay["RXY_HOST_TRIPLE"]    = opts.host_triple;
    popts.env_overlay["RXY_TARGET_TRIPLE"]  = opts.target_triple;
    popts.env_overlay["RXY_PROFILE"]        = opts.profile_name;
    popts.env_overlay["RXY_PKG_NAME"]       = m.package.name;
    popts.env_overlay["RXY_PKG_VERSION"]    = m.package.version;
    process::Result pr = process::run(script_bin, /*args*/ {}, popts);

    // 4) FR-025 enforcement: source tree must be unchanged.
    detect_source_mutation(m.package_root, before_mtimes);

    if (pr.exit_code != 0) {
        // Persist stderr for debugging.
        util::atomic_write_text_file(cache_dir / "stderr", pr.stderr_data);
        throw std::runtime_error("build script `" + m.build->script->string() +
            "` exited " + std::to_string(pr.exit_code) +
            (pr.stderr_data.empty() ? "" : " — stderr:\n" + pr.stderr_data));
    }

    // 5) Parse stdout, persist the output + invoked.toml for caching.
    Directives d = parse_stdout(pr.stdout_data);

    util::atomic_write_text_file(cache_dir / "output", pr.stdout_data);
    if (!pr.stderr_data.empty()) {
        util::atomic_write_text_file(cache_dir / "stderr", pr.stderr_data);
    }
    std::map<std::string, std::string> env_now;
    for (const auto& v : d.rerun_if_env_changed) {
        const char* p = std::getenv(v.c_str());
        env_now[v] = p ? p : "";
    }
    persist_invoked(cache_dir / "invoked.toml", d, env_now);

    // 6) Surface warnings + recorded directives.
    for (const auto& w : d.warnings) {
        auto diag_obj = diag::Diagnostic::warning(w);
        diag::print(diag_obj);
    }
    auto warn_recorded = [&](const std::string& kind, const std::string& v) {
        if (opts.quiet) return;
        diag::print(diag::Diagnostic::warning(
            "rxy:" + kind + "=" + v + " recorded but not yet wired into rexc")
            .with_help("link/cfg directives become functional once rexc accepts linker flags (Phase E)"));
    };
    for (const auto& v : d.link_libs)     warn_recorded("link-lib", v);
    for (const auto& v : d.link_searches) warn_recorded("link-search", v);
    for (const auto& v : d.cfgs)          warn_recorded("cfg", v);

    return d;
}

// ----------------------------------------------------------------------------
// Transitive allow-list check
// ----------------------------------------------------------------------------

std::vector<std::string>
disallowed_transitive_scripts(const manifest::Manifest& root,
                                const std::vector<manifest::Manifest>& deps) {
    std::set<std::string> allowed;
    if (root.build) {
        for (const auto& a : root.build->allow_scripts) allowed.insert(a);
    }
    std::set<std::string> direct;
    for (const auto& d : root.dependencies)     direct.insert(d.name);
    for (const auto& d : root.dev_dependencies) direct.insert(d.name);

    std::vector<std::string> blocked;
    for (const auto& d : deps) {
        if (!d.build || !d.build->script) continue;
        if (direct.count(d.package.name))         continue;     // direct deps run by default
        if (allowed.count(d.package.name))        continue;     // explicitly allowed
        blocked.push_back(d.package.name);
    }
    return blocked;
}

std::string blocked_diagnostic_text(const std::vector<std::string>& blocked,
                                     const std::vector<manifest::Manifest>& deps) {
    std::ostringstream oss;
    oss << blocked.size() << " transitive build script(s) blocked";
    return oss.str();
}

}  // namespace rxy::build_script
