#include "source/source.hpp"

#include "cache/cache.hpp"
#include "hash/sha256.hpp"
#include "manifest/manifest.hpp"
#include "process/process.hpp"
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

Resolved resolve_git(const manifest::DependencySpec& dep) {
    fs::path git_exe = find_git();
    fs::path bare    = cache::git_db_path_for_url(*dep.git_url);

    if (!fs::is_directory(bare / "objects")) {
        // First time we see this URL — clone bare.
        std::error_code ec;
        fs::remove_all(bare, ec);
        cache::ensure_dir(bare.parent_path());
        run_git(git_exe,
                {"clone", "--bare", "--quiet", *dep.git_url, bare.string()},
                bare.parent_path(),
                "clone " + *dep.git_url);
    } else {
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

Resolved resolve(const manifest::DependencySpec& dep,
                 const fs::path& importer_root) {
    if (dep.is_path()) return resolve_path(dep, importer_root);
    if (dep.is_git())  return resolve_git(dep);
    if (dep.is_registry()) {
        throw std::runtime_error("registry dependency `" + dep.name +
            " = \"" + *dep.registry_version + "\"` requires Phase C; "
            "for now, pin via `git = \"...\", tag = \"v...\"`");
    }
    throw std::runtime_error("dependency `" + dep.name + "` has no source");
}

}  // namespace rxy::source
