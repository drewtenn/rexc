#include "source/source.hpp"

#include "cache/cache.hpp"
#include "hash/sha256.hpp"
#include "manifest/manifest.hpp"
#include "process/process.hpp"
#include "registry/registry.hpp"
#include "semver/semver.hpp"
#include "util/fs.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace rxy::source {

namespace fs = std::filesystem;

namespace {

void load_dep_manifest_into(Resolved& out, const fs::path& root) {
    fs::path manifest_toml = root / "Rexy.toml";
    auto loaded = manifest::load_manifest(manifest_toml);
    if (auto* errs = std::get_if<std::vector<diag::Diagnostic>>(&loaded)) {
        std::ostringstream oss;
        oss << "dependency at " << root.string() << " has invalid Rexy.toml:";
        for (const auto& d : *errs) oss << "\n  " << d.message;
        throw std::runtime_error(oss.str());
    }
    auto m = std::get<manifest::Manifest>(loaded);
    if (!out.name.empty() && out.name != m.package.name) {
        // Rxy convention: import handle == package name. If they differ,
        // that's a future feature (dep alias). For Phase B, error.
        throw std::runtime_error("dependency name mismatch: requested as `" +
            out.name + "` but its [package].name is `" + m.package.name + "`");
    }
    out.name    = m.package.name;
    out.version = m.package.version;
    out.package_root = root;
}

fs::path find_git() {
    if (const char* env = std::getenv("GIT")) {
        if (*env) return fs::path(env);
    }
    // Use posix_spawn-friendly absolute path lookup via PATH.
    const char* path_env = std::getenv("PATH");
    if (!path_env) throw std::runtime_error("git: PATH is unset");
    std::string p = path_env;
    std::string::size_type start = 0;
    while (start <= p.size()) {
        std::string::size_type end = p.find(':', start);
        std::string entry = (end == std::string::npos) ? p.substr(start) : p.substr(start, end - start);
        if (!entry.empty()) {
            fs::path c = fs::path(entry) / "git";
            std::error_code ec;
            if (fs::is_regular_file(c, ec)) return c;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    throw std::runtime_error("could not find `git` on PATH; install git or set $GIT");
}

void run_git(const fs::path& git_exe,
             const std::vector<std::string>& args,
             const fs::path& cwd,
             const std::string& what) {
    process::Options popts;
    popts.cwd = cwd;
    popts.stream_through = false;
    auto pr = process::run(git_exe, args, popts);
    if (pr.exit_code != 0) {
        std::ostringstream oss;
        oss << "git " << what << " failed (exit " << pr.exit_code << ")";
        if (!pr.stderr_data.empty()) oss << ":\n" << pr.stderr_data;
        throw std::runtime_error(oss.str());
    }
}

std::string capture_git(const fs::path& git_exe,
                         const std::vector<std::string>& args,
                         const fs::path& cwd,
                         const std::string& what) {
    process::Options popts;
    popts.cwd = cwd;
    popts.stream_through = false;
    auto pr = process::run(git_exe, args, popts);
    if (pr.exit_code != 0) {
        std::ostringstream oss;
        oss << "git " << what << " failed (exit " << pr.exit_code << ")";
        if (!pr.stderr_data.empty()) oss << ":\n" << pr.stderr_data;
        throw std::runtime_error(oss.str());
    }
    while (!pr.stdout_data.empty() &&
           (pr.stdout_data.back() == '\n' || pr.stdout_data.back() == '\r')) {
        pr.stdout_data.pop_back();
    }
    return pr.stdout_data;
}

Resolved resolve_path(const manifest::DependencySpec& dep,
                       const fs::path& importer_root) {
    fs::path resolved = *dep.path;
    if (resolved.is_relative()) resolved = importer_root / resolved;
    std::error_code ec;
    resolved = fs::canonical(resolved, ec);
    if (ec) {
        throw std::runtime_error("path dependency `" + dep.name +
            "` could not be canonicalized: " + dep.path->string() +
            " (" + ec.message() + ")");
    }
    if (!fs::is_directory(resolved)) {
        throw std::runtime_error("path dependency `" + dep.name +
            "` is not a directory: " + resolved.string());
    }
    Resolved r;
    r.name = dep.name;
    r.source_token = "path+" + resolved.string();
    load_dep_manifest_into(r, resolved);
    return r;
}

Resolved resolve_git(const manifest::DependencySpec& dep, const ResolveOptions& opts) {
    fs::path git_exe = find_git();
    fs::path bare    = cache::git_db_path_for_url(*dep.git_url);

    if (!fs::is_directory(bare / "objects")) {
        // First time we see this URL — clone bare.
        if (opts.offline) {
            throw std::runtime_error(
                "offline mode: no cached clone for `" + dep.name +
                "` at " + bare.string() +
                "\n  --offline disables git clone; run once online to prime the cache");
        }
        std::error_code ec;
        fs::remove_all(bare, ec);
        cache::ensure_dir(bare.parent_path());
        run_git(git_exe,
                {"clone", "--bare", "--quiet", *dep.git_url, bare.string()},
                bare.parent_path(),
                "clone " + *dep.git_url);
    } else if (!opts.offline) {
        // Make sure refs are fresh. `--force` lets moved tags update so we
        // can *detect* drift downstream rather than fail here.
        run_git(git_exe, {"fetch", "--all", "--quiet", "--tags", "--force"},
                 bare, "fetch in " + bare.string());
    }

    // Resolve the requested ref into a concrete commit SHA.
    std::string ref;
    if      (dep.git_rev)    ref = *dep.git_rev;
    else if (dep.git_tag)    ref = "refs/tags/" + *dep.git_tag;
    else if (dep.git_branch) ref = *dep.git_branch;
    else                     ref = "HEAD";

    std::string commit = capture_git(git_exe,
        {"rev-parse", ref + "^{commit}"},
        bare,
        "rev-parse " + ref);

    fs::path workdir = cache::src_path(dep.name, commit);
    if (!fs::exists(workdir / ".rxy_extracted")) {
        std::error_code ec;
        fs::remove_all(workdir, ec);
        cache::ensure_dir(workdir);
        // Materialize a worktree at this commit. `git --git-dir=<bare> archive`
        // is the simplest portable mechanism: produce a tar of the commit
        // contents and extract.
        process::Options popts;
        popts.cwd = workdir;
        popts.stream_through = false;
        std::vector<std::string> archive_args = {
            "--git-dir=" + bare.string(),
            "archive", "--format=tar", commit
        };
        // We rely on shell-style piping; instead we capture the tar in memory.
        process::Result pr = process::run(git_exe, archive_args, popts);
        if (pr.exit_code != 0) {
            throw std::runtime_error("git archive failed for " + dep.name +
                " @ " + commit + " (exit " + std::to_string(pr.exit_code) + "):\n" +
                pr.stderr_data);
        }
        // Pipe the tar bytes into `tar -x -C <workdir>`.
        fs::path tar_exe = "/usr/bin/tar";
        if (!fs::is_regular_file(tar_exe)) tar_exe = "/bin/tar";
        // Write tarball to a temp file (simpler than wiring a pipe to a 2nd child).
        fs::path tmp_tar = workdir / ".tmp.tar";
        util::atomic_write_text_file(tmp_tar, pr.stdout_data);
        process::Options topts;
        topts.cwd = workdir;
        topts.stream_through = false;
        process::Result tr = process::run(tar_exe, {"-xf", tmp_tar.string()}, topts);
        if (tr.exit_code != 0) {
            throw std::runtime_error("tar -xf failed for " + dep.name + ": " + tr.stderr_data);
        }
        fs::remove(tmp_tar, ec);
        // Marker so we skip extraction next time.
        std::ofstream(workdir / ".rxy_extracted") << "ok\n";
    }

    Resolved r;
    r.name = dep.name;
    std::string token = "git+" + *dep.git_url;
    if      (dep.git_rev)    token += "?rev=" + *dep.git_rev;
    else if (dep.git_tag)    token += "?tag=" + *dep.git_tag;
    else if (dep.git_branch) token += "?branch=" + *dep.git_branch;
    r.source_token = token;
    r.commit       = commit;
    load_dep_manifest_into(r, workdir);
    r.checksum     = hash::sha256_dir_tree(workdir);
    return r;
}

}  // namespace

namespace {

// Phase C: resolve via the configured registry. Reuses the git-fetch path
// after picking the highest non-yanked version that matches the constraint.
Resolved resolve_registry_dep(const manifest::DependencySpec& dep, const ResolveOptions& opts) {
    auto reg = registry::open_default();
    if (!opts.offline) registry::refresh_if_stale(reg);

    auto info = reg.lookup(dep.name);
    if (!info) {
        throw std::runtime_error("`" + dep.name + "` not found in registry `" +
            reg.config.name + "`");
    }
    auto constraint_str = dep.registry_version.value_or("*");
    auto constraint = semver::parse_constraint(constraint_str);
    if (!constraint) {
        throw std::runtime_error("invalid version constraint `" + constraint_str +
            "` for `" + dep.name + "`");
    }
    std::vector<semver::Version> candidates;
    for (const auto& e : info->entries) {
        if (e.yanked) continue;
        candidates.push_back(e.version);
    }
    auto best = semver::resolve_highest(*constraint, candidates);
    if (!best) {
        std::ostringstream oss;
        oss << "no version of `" << dep.name << "` matches `" << constraint_str << "`";
        if (!info->entries.empty()) {
            oss << " (available:";
            for (const auto& e : info->entries) {
                oss << " " << e.version.to_string();
                if (e.yanked) oss << "(yanked)";
            }
            oss << ")";
        }
        throw std::runtime_error(oss.str());
    }

    // Find the matching entry to read git-url + commit + checksum.
    const registry::Entry* entry = nullptr;
    for (const auto& e : info->entries) {
        if (e.version == *best && !e.yanked) { entry = &e; break; }
    }
    if (!entry) throw std::runtime_error("internal: resolved version not found");

    // Reuse the git fetch pipeline by synthesizing a DependencySpec.
    manifest::DependencySpec synth;
    synth.name = dep.name;
    synth.git_url = entry->git_url;
    synth.git_rev = entry->commit;
    Resolved r = resolve_git(synth, opts);
    // Verify: registry's published checksum must match what we hashed.
    if (r.checksum && *r.checksum != entry->checksum) {
        throw std::runtime_error(
            "registry checksum mismatch for `" + dep.name + " " + best->to_string() +
            "`: registry says " + entry->checksum + " but fetched tree hashes to " +
            *r.checksum + ". This may indicate a compromised source.");
    }
    // Use the version we resolved (registry's, not the dep's manifest's, in
    // case the upstream package's Rexy.toml drifted from the registry's record).
    r.version = best->to_string();
    r.source_token = "registry+" + reg.config.name;
    return r;
}

}  // namespace

// FR-046/047 partial: the bundled stdlib is exposed via an in-rxy constant.
// A future Phase F+1 swaps this for a real package fetch.
#ifndef RXY_BUNDLED_STDLIB_VERSION
#define RXY_BUNDLED_STDLIB_VERSION "0.1.0"
#endif

Resolved resolve_bundled_stdlib(const manifest::DependencySpec& dep) {
    std::string bundled = RXY_BUNDLED_STDLIB_VERSION;
    auto bundled_v = semver::parse_version(bundled);
    if (!bundled_v) {
        throw std::runtime_error("internal: invalid RXY_BUNDLED_STDLIB_VERSION");
    }
    std::string constraint_str = dep.registry_version.value_or("*");
    auto constraint = semver::parse_constraint(constraint_str);
    if (!constraint) {
        throw std::runtime_error("invalid stdlib version constraint `" + constraint_str + "`");
    }
    if (!constraint->matches(*bundled_v)) {
        throw std::runtime_error(
            "stdlib pin `" + constraint_str + "` does not accept the bundled stdlib " +
            bundled + "\n  use `std = \"" + bundled + "\"` or remove the pin to use the bundled stdlib");
    }
    Resolved r;
    r.name = "std";
    r.version = bundled;
    r.source_token = "bundled+rexc";
    // No package_root, no commit, no checksum — the stdlib lives inside rexc.
    return r;
}

Resolved resolve(const manifest::DependencySpec& dep,
                 const fs::path& importer_root,
                 const ResolveOptions& opts) {
    // FR-046/047: `std` is a reserved dep name that resolves against the
    // bundled stdlib until Phase F+1 unbundles it.
    if (dep.name == "std" && (dep.is_registry() || (!dep.is_path() && !dep.is_git()))) {
        return resolve_bundled_stdlib(dep);
    }
    if (dep.is_path()) return resolve_path(dep, importer_root);
    if (dep.is_git())  return resolve_git(dep, opts);
    if (dep.is_registry()) return resolve_registry_dep(dep, opts);
    throw std::runtime_error("dependency `" + dep.name + "` has no source");
}

}  // namespace rxy::source
