#include "build/build.hpp"

#include "diag/diag.hpp"
#include "lockfile/lockfile.hpp"
#include "process/process.hpp"
#include "resolver/resolver.hpp"
#include "source/source.hpp"
#include "util/fs.hpp"
#include "util/tty.hpp"

#include <chrono>
#include <filesystem>
#include <sstream>

namespace rxy::build {

namespace fs = std::filesystem;

namespace {

fs::path ensure_target_dir(const manifest::Manifest& m, const std::string& profile) {
    fs::path dir = m.package_root / "target" / profile;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

// Returns the absolute source path for a target, ensuring it exists.
std::optional<fs::path> resolve_target_path(const manifest::Manifest& m,
                                             const fs::path& rel) {
    fs::path abs = m.package_root / rel;
    std::error_code ec;
    if (!fs::is_regular_file(abs, ec)) return std::nullopt;
    return fs::canonical(abs, ec);
}

std::string profile_label(const std::string& name, const manifest::Profile& p) {
    std::ostringstream oss;
    oss << name;
    if (name == "dev")          oss << " [unoptimized + debuginfo]";
    else if (name == "release") oss << " [optimized";
                                if (p.lto.value_or(false)) oss << " + lto";
                                if (p.strip.value_or(false)) oss << " + stripped";
                                if (name == "release") oss << "]";
    return oss.str();
}

}  // namespace

Result run(const manifest::Manifest& m, const Options& opts) {
    Result r;
    auto t0 = std::chrono::steady_clock::now();

    manifest::Profile profile = m.resolved_profile(opts.profile_name);
    fs::path target_dir = ensure_target_dir(m, opts.profile_name);

    // Resolve dependency graph (Phase B). Path deps are resolved in place;
    // git deps are cloned/checked-out into ~/.rxy/src.
    resolver::Resolution resolution;
    if (!m.dependencies.empty()) {
        if (!opts.quiet) {
            diag::status("Resolving",
                std::to_string(m.dependencies.size()) + " dependencies");
        }
        try {
            resolution = resolver::resolve_graph(m);
        } catch (const std::exception& ex) {
            diag::print(diag::Diagnostic::error(ex.what()));
            r.exit_code = 1;
            return r;
        }
    }

    // Refresh the lockfile.
    {
        lockfile::Lockfile fresh = m.dependencies.empty()
            ? lockfile::from_manifest_phase_a(m)
            : lockfile::from_resolution(m, resolution);
        fs::path lock_path = m.package_root / "Rexy.lock";

        // Drift detection (FR-013) — if there's an existing lockfile and
        // a previously-pinned dep now resolves to a different commit/checksum,
        // bail out.
        if (auto previous = lockfile::read(lock_path)) {
            if (auto drift = lockfile::detect_drift(*previous, fresh)) {
                diag::print(diag::Diagnostic::error(*drift)
                    .with_help("run `rxy build` (without --locked) to re-pin if intentional"));
                r.exit_code = 1;
                return r;
            }
        }

        if (opts.locked) {
            // FR-011: --locked → fail if the lockfile would change.
            auto previous = lockfile::read(lock_path);
            if (!previous) {
                diag::print(diag::Diagnostic::error(
                    "no Rexy.lock found, but --locked was passed")
                    .with_help("run `rxy build` once without --locked to generate it"));
                r.exit_code = 1;
                return r;
            }
            if (!lockfile::equal(*previous, fresh)) {
                diag::print(diag::Diagnostic::error(
                    "Rexy.lock would be modified, but --locked was passed")
                    .with_help("run `rxy build` without --locked to update Rexy.lock"));
                r.exit_code = 1;
                return r;
            }
        } else {
            lockfile::write(lock_path, fresh);
        }
    }

    fs::path rexc;
    try {
        rexc = util::find_rexc(util::current_executable_path());
    } catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(ex.what()));
        r.exit_code = 1;
        return r;
    }

    // Determine which bins to compile.
    std::vector<manifest::BinTarget> selected_bins;
    if (opts.bin) {
        auto found = m.find_bin(*opts.bin);
        if (!found) {
            diag::print(diag::Diagnostic::error(
                "no binary named `" + *opts.bin + "` found in this package")
                .with_help("declare it under [[targets.bin]] or omit --bin"));
            r.exit_code = 1;
            return r;
        }
        selected_bins.push_back(*found);
    } else {
        selected_bins = m.bins;
    }

    if (selected_bins.empty() && !m.lib) {
        diag::print(diag::Diagnostic::error(
            "no targets to build")
            .with_help("declare [[targets.bin]] or [targets.lib], or place src/main.rx"));
        r.exit_code = 1;
        return r;
    }

    // Compute --package-path values once per build, derived from the
    // resolved dep graph. For each dep, the import handle is the dep's
    // package name (Phase B convention: package.name == import name).
    // We pass the directory containing that dep's lib target's source file
    // (or the package root if no lib target).
    std::vector<std::string> dep_package_paths;
    for (const auto& d : resolution.packages) {
        // Reload the dep's manifest to know where its lib lives.
        auto loaded = manifest::load_manifest(d.package_root / "Rexy.toml");
        if (auto* errs = std::get_if<std::vector<diag::Diagnostic>>(&loaded)) {
            for (const auto& e : *errs) diag::print(e);
            r.exit_code = 1;
            return r;
        }
        auto dm = std::get<manifest::Manifest>(loaded);
        fs::path search_root;
        if (dm.lib) {
            fs::path lib_abs = (d.package_root / dm.lib->path).lexically_normal();
            search_root = lib_abs.parent_path();
        } else {
            // No lib target: fall back to the dep's package root. Importer
            // can still `mod <dep-name>;` and find <dep-name>.rx there.
            search_root = d.package_root;
        }
        // Sanity: rexc requires the path to be a directory.
        if (fs::is_directory(search_root)) {
            dep_package_paths.push_back(search_root.string());
        }
    }

    // Phase A: lib targets are syntax-checked by compiling them into an unused .o,
    // matching rexc's `-c` behavior. The artifact lives under target/<profile>/lib/.
    if (m.lib) {
        auto src = resolve_target_path(m, m.lib->path);
        if (!src) {
            diag::print(diag::Diagnostic::error(
                "lib target source not found: " + m.lib->path.string()));
            r.exit_code = 1;
            return r;
        }
        if (!opts.quiet) {
            diag::status("Compiling",
                m.package.name + " v" + m.package.version + " (lib)");
        }
        fs::path out = target_dir / (m.package.name + ".o");
        std::vector<std::string> args = {src->string(), "-c", "-o", out.string()};
        for (const auto& pp : dep_package_paths) {
            args.push_back("--package-path");
            args.push_back(pp);
        }
        process::Options popts;
        popts.cwd = m.package_root;
        popts.stream_through = true;
        process::Result pr = process::run(rexc, args, popts);
        if (pr.exit_code != 0) {
            diag::print(diag::Diagnostic::error(
                "rexc failed for lib (exit " + std::to_string(pr.exit_code) + ")"));
            r.exit_code = pr.exit_code;
            return r;
        }
        r.artifacts.push_back(out);
    }

    for (const auto& bin : selected_bins) {
        auto src = resolve_target_path(m, bin.path);
        if (!src) {
            diag::print(diag::Diagnostic::error(
                "bin target source not found: " + bin.path.string()));
            r.exit_code = 1;
            return r;
        }

        if (!opts.quiet) {
            diag::status("Compiling",
                m.package.name + " v" + m.package.version + " (bin " + bin.name + ")");
        }

        fs::path out = target_dir / bin.name;
        std::vector<std::string> args = {src->string(), "-o", out.string()};
        for (const auto& pp : dep_package_paths) {
            args.push_back("--package-path");
            args.push_back(pp);
        }
        process::Options popts;
        popts.cwd = m.package_root;
        popts.stream_through = true;
        process::Result pr = process::run(rexc, args, popts);
        if (pr.exit_code != 0) {
            diag::print(diag::Diagnostic::error(
                "rexc failed for bin `" + bin.name + "` (exit " +
                std::to_string(pr.exit_code) + ")"));
            r.exit_code = pr.exit_code;
            return r;
        }
        r.artifacts.push_back(out);
    }

    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    if (!opts.quiet) {
        diag::finished_summary(profile_label(opts.profile_name, profile), secs);
    }

    return r;
}

}  // namespace rxy::build
