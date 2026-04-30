#include "cli/cli.hpp"

#include "build/build.hpp"
#include "diag/diag.hpp"
#include "hash/sha256.hpp"
#include "lockfile/lockfile.hpp"
#include "manifest/manifest.hpp"
#include "process/process.hpp"
#include "registry/registry.hpp"
#include "semver/semver.hpp"
#include "source/source.hpp"
#include "util/fs.hpp"
#include "util/tty.hpp"
#include "workspace/workspace.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace rxy::cli {

namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion =
#ifdef RXY_VERSION
    RXY_VERSION
#else
    "0.0.0-dev"
#endif
    ;

void print_usage(FILE* out = stderr) {
    std::fprintf(out,
        "rxy %s — Rexy package manager\n"
        "\n"
        "Usage: rxy <COMMAND> [FLAGS] [ARGS]\n"
        "\n"
        "Commands:\n"
        "  new <NAME> [--lib | --bin]   Create a new project at ./<NAME>\n"
        "  init [--lib | --bin]         Initialize current dir as a Rexy package\n"
        "  build [OPTIONS]              Compile all targets of the current package\n"
        "  run [--bin NAME] [-- ARGS]   Build then run a binary\n"
        "  --version                    Print version\n"
        "  --help                       Print this help\n"
        "\n"
        "Build options:\n"
        "  --release                    Use [profile.release]\n"
        "  --profile NAME               Use [profile.<NAME>]\n"
        "  -q, --quiet                  Suppress status lines\n"
        "  -v, --verbose                Show resolution chain\n"
        "  --color WHEN                 auto (default), always, never\n"
        "  --manifest-path PATH         Operate on the manifest at PATH\n",
        kVersion);
}

bool parse_color(const std::string& s, util::ColorMode& out) {
    if (s == "auto")   { out = util::ColorMode::Auto;   return true; }
    if (s == "always") { out = util::ColorMode::Always; return true; }
    if (s == "never")  { out = util::ColorMode::Never;  return true; }
    return false;
}

// Splits argv after the program name into (global_flags, subcommand, sub_args).
struct Parsed {
    GlobalFlags g;
    std::string subcommand;
    std::vector<std::string> sub_args;
    bool show_help    = false;
    bool show_version = false;
    bool error        = false;
};

// Splits "--key=value" into ("--key", "value"). Returns false for non-equals
// args.
bool split_eq(const std::string& a, std::string& key, std::string& val) {
    if (a.size() < 3 || a.substr(0, 2) != "--") return false;
    auto pos = a.find('=');
    if (pos == std::string::npos) return false;
    key = a.substr(0, pos);
    val = a.substr(pos + 1);
    return true;
}

Parsed parse_argv(int argc, char** argv) {
    Parsed p;
    int i = 1;
    while (i < argc) {
        std::string a = argv[i];

        std::string eq_key, eq_val;
        bool has_eq = split_eq(a, eq_key, eq_val);
        const std::string& flag = has_eq ? eq_key : a;

        auto take_value = [&](const char* flag_name) -> std::optional<std::string> {
            if (has_eq) return eq_val;
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s requires an argument\n", flag_name);
                p.error = true;
                return std::nullopt;
            }
            return std::string{argv[++i]};
        };

        if (flag == "--help" || flag == "-h") { p.show_help = true; ++i; continue; }
        if (flag == "--version") { p.show_version = true; ++i; continue; }
        if (flag == "-q" || flag == "--quiet")   { p.g.quiet = true;   ++i; continue; }
        if (flag == "-v" || flag == "--verbose") { p.g.verbose = true; ++i; continue; }
        if (flag == "--offline") { p.g.offline = true; ++i; continue; }

        if (flag == "--color") {
            auto v = take_value("--color");
            if (!v) return p;
            if (!parse_color(*v, p.g.color)) {
                std::fprintf(stderr, "error: invalid --color value (use auto|always|never)\n");
                p.error = true; return p;
            }
            ++i; continue;
        }
        if (flag == "--manifest-path") {
            auto v = take_value("--manifest-path");
            if (!v) return p;
            p.g.manifest_path = fs::path(*v);
            ++i; continue;
        }
        if (!a.empty() && a[0] != '-') {
            p.subcommand = a;
            ++i;
            while (i < argc) p.sub_args.emplace_back(argv[i++]);
            return p;
        }
        std::fprintf(stderr, "error: unknown global flag `%s`\n", a.c_str());
        p.error = true; return p;
    }
    return p;
}

fs::path resolve_manifest_path(const GlobalFlags& g) {
    if (g.manifest_path) return fs::absolute(*g.manifest_path);
    auto root = util::find_manifest_root(fs::current_path());
    if (!root) {
        throw std::runtime_error("could not find Rexy.toml in this directory or any parent");
    }
    return *root / "Rexy.toml";
}

// Loads a member's manifest and applies workspace inheritance if a workspace
// root is found. Returns either a fully-resolved Manifest or a list of errors.
std::variant<manifest::Manifest, std::vector<diag::Diagnostic>>
load_with_workspace(const fs::path& mp) {
    auto loaded = manifest::load_manifest(mp);
    if (auto* errs = std::get_if<std::vector<diag::Diagnostic>>(&loaded)) {
        return *errs;
    }
    auto m = std::get<manifest::Manifest>(loaded);

    auto ws_root = workspace::find_workspace_root(m.package_root.parent_path());
    if (!ws_root) return m;
    fs::path ws_manifest = *ws_root / "Rexy.toml";
    if (ws_manifest == m.manifest_path) return m;     // root IS this manifest
    workspace::Workspace ws;
    try {
        ws = workspace::load(ws_manifest);
    } catch (const std::exception& ex) {
        std::vector<diag::Diagnostic> errs;
        errs.push_back(diag::Diagnostic::error(ex.what()));
        return errs;
    }
    auto inherit_errs = workspace::apply_inheritance(m, ws);
    if (!inherit_errs.empty()) return inherit_errs;
    m.workspace_root = ws.root;
    return m;
}

int cmd_publish(const std::vector<std::string>& args, const GlobalFlags& g);
int cmd_yank_or_unyank(const std::vector<std::string>& args, bool yank);
int cmd_add(const std::vector<std::string>& args, const GlobalFlags& g);
int cmd_remove(const std::vector<std::string>& args, const GlobalFlags& g);
int cmd_test(const std::vector<std::string>& args, const GlobalFlags& g);

int cmd_new_or_init(const std::vector<std::string>& args, bool is_init) {
    bool want_lib = false;
    bool want_bin = false;
    std::string name;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "--lib") want_lib = true;
        else if (a == "--bin") want_bin = true;
        else if (!a.empty() && a[0] != '-' && name.empty()) name = a;
        else {
            std::fprintf(stderr, "error: unexpected argument `%s`\n", a.c_str());
            return 2;
        }
    }

    if (want_lib && want_bin) {
        std::fprintf(stderr, "error: --lib and --bin are mutually exclusive\n");
        return 2;
    }
    if (!want_lib && !want_bin) want_bin = true;  // default

    fs::path target_dir;
    std::string pkg_name;
    if (is_init) {
        target_dir = fs::current_path();
        pkg_name = !name.empty() ? name : target_dir.filename().string();
    } else {
        if (name.empty()) {
            std::fprintf(stderr, "error: `rxy new` requires a name\n");
            return 2;
        }
        target_dir = fs::current_path() / name;
        pkg_name = name;
        std::error_code ec;
        if (fs::exists(target_dir, ec)) {
            std::fprintf(stderr, "error: destination `%s` already exists\n",
                         target_dir.string().c_str());
            return 1;
        }
        fs::create_directories(target_dir, ec);
    }

    fs::path src_dir = target_dir / "src";
    std::error_code ec;
    fs::create_directories(src_dir, ec);

    fs::path manifest = target_dir / "Rexy.toml";
    if (fs::exists(manifest, ec)) {
        std::fprintf(stderr, "error: Rexy.toml already exists at `%s`\n",
                     manifest.string().c_str());
        return 1;
    }

    {
        std::ostringstream oss;
        oss << "[package]\n"
            << "name = \""    << pkg_name << "\"\n"
            << "version = \"0.1.0\"\n"
            << "edition = \"2026\"\n"
            << "\n"
            << "# rxy Phase A scope: manifest, lockfile, single-package build.\n"
            << "# Add [dependencies], [build], [workspace] in later phases.\n";
        if (want_lib) {
            oss << "\n[targets.lib]\n"
                << "path = \"src/lib.rx\"\n";
        }
        if (want_bin) {
            oss << "\n[[targets.bin]]\n"
                << "name = \"" << pkg_name << "\"\n"
                << "path = \"src/main.rx\"\n";
        }
        util::atomic_write_text_file(manifest, oss.str());
    }

    if (want_bin) {
        fs::path main_rx = src_dir / "main.rx";
        if (!fs::exists(main_rx)) {
            std::ofstream out(main_rx);
            out << "fn main() -> i32 {\n"
                << "    return 0;\n"
                << "}\n";
        }
    }
    if (want_lib) {
        fs::path lib_rx = src_dir / "lib.rx";
        if (!fs::exists(lib_rx)) {
            std::ofstream out(lib_rx);
            out << "pub fn add(a: i32, b: i32) -> i32 {\n"
                << "    return a + b;\n"
                << "}\n";
        }
    }

    fs::path gitignore = target_dir / ".gitignore";
    if (!fs::exists(gitignore)) {
        std::ofstream out(gitignore);
        out << "/target/\n";
    }

    diag::status(is_init ? "Initialized" : "Created",
                 std::string(want_bin ? "binary" : "library") + " `" + pkg_name + "` package");
    return 0;
}

int cmd_build(const std::vector<std::string>& args, const GlobalFlags& g) {
    build::Options bopts;
    bopts.profile_name = "dev";
    bopts.quiet   = g.quiet;
    bopts.verbose = g.verbose;
    bopts.color_for_rexc = util::color_enabled_for_stderr();
    bopts.offline = g.offline;

    for (size_t i = 0; i < args.size(); ++i) {
        std::string a = args[i];
        std::string eq_key, eq_val;
        bool has_eq = a.size() > 2 && a.substr(0, 2) == "--";
        if (has_eq) {
            auto pos = a.find('=');
            if (pos != std::string::npos) {
                eq_key = a.substr(0, pos);
                eq_val = a.substr(pos + 1);
            } else {
                has_eq = false;
            }
        }
        const std::string& flag = has_eq ? eq_key : a;
        auto take = [&](const char* name) -> std::optional<std::string> {
            if (has_eq) return eq_val;
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "error: %s requires an argument\n", name);
                return std::nullopt;
            }
            return args[++i];
        };

        if      (flag == "--release") bopts.profile_name = "release";
        else if (flag == "--locked")  bopts.locked = true;
        else if (flag == "--no-build-scripts")      bopts.no_build_scripts = true;
        else if (flag == "--allow-all-build-scripts") bopts.allow_all_build_scripts = true;
        else if (flag == "--profile") {
            auto v = take("--profile"); if (!v) return 2;
            bopts.profile_name = *v;
        }
        else if (flag == "--bin") {
            auto v = take("--bin"); if (!v) return 2;
            bopts.bin = *v;
        }
        else if (flag == "--target") {
            auto v = take("--target"); if (!v) return 2;
            bopts.target_triple = *v;
        }
        else {
            std::fprintf(stderr, "error: unknown build option `%s`\n", a.c_str());
            return 2;
        }
    }

    fs::path mp;
    try {
        mp = resolve_manifest_path(g);
    } catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(ex.what())
            .with_help("run `rxy new <name>` or `rxy init` first"));
        return 1;
    }

    auto loaded = load_with_workspace(mp);
    if (auto* errs = std::get_if<std::vector<diag::Diagnostic>>(&loaded)) {
        for (const auto& d : *errs) diag::print(d);
        return 1;
    }
    auto m = std::get<manifest::Manifest>(loaded);

    auto r = build::run(m, bopts);
    return r.exit_code;
}

int cmd_run(const std::vector<std::string>& args, const GlobalFlags& g) {
    std::optional<std::string> bin;
    std::vector<std::string> child_args;
    bool seen_dashdash = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (seen_dashdash) { child_args.push_back(a); continue; }
        if (a == "--") { seen_dashdash = true; continue; }
        if (a == "--bin") {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "error: --bin requires an argument\n");
                return 2;
            }
            bin = args[++i];
        } else {
            std::fprintf(stderr, "error: unknown run option `%s` (use `--` to separate child args)\n", a.c_str());
            return 2;
        }
    }

    fs::path mp;
    try {
        mp = resolve_manifest_path(g);
    } catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(ex.what()));
        return 1;
    }

    auto loaded = load_with_workspace(mp);
    if (auto* errs = std::get_if<std::vector<diag::Diagnostic>>(&loaded)) {
        for (const auto& d : *errs) diag::print(d);
        return 1;
    }
    auto m = std::get<manifest::Manifest>(loaded);

    build::Options bopts;
    bopts.profile_name = "dev";
    bopts.quiet   = g.quiet;
    bopts.verbose = g.verbose;
    bopts.bin     = bin;
    bopts.color_for_rexc = util::color_enabled_for_stderr();
    auto br = build::run(m, bopts);
    if (br.exit_code != 0) return br.exit_code;

    auto target = m.find_bin(bin.value_or(""));
    if (!target) {
        diag::print(diag::Diagnostic::error(
            bin ? "no binary named `" + *bin + "`"
                : "multiple binaries — pass --bin <name>"));
        return 1;
    }

    fs::path artifact = m.package_root / "target" / bopts.profile_name / target->name;
    if (!fs::is_regular_file(artifact)) {
        diag::print(diag::Diagnostic::error(
            "build succeeded but artifact missing: " + artifact.string()));
        return 1;
    }

    if (!g.quiet) {
        diag::status("Running", "`" + artifact.string() + "`");
    }

    process::Options popts;
    popts.cwd = m.package_root;
    popts.stream_through = true;
    auto pr = process::run(artifact, child_args, popts);
    return pr.exit_code;
}

// ===== Phase C: registry-aware commands =====

namespace {

std::string rfc3339_now_utc() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

}  // namespace

int cmd_publish(const std::vector<std::string>& args, const GlobalFlags& g) {
    bool dry_run = false;
    std::string registry_name = "default";
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "--dry-run") dry_run = true;
        else if (a == "--registry") {
            if (i + 1 >= args.size()) { std::fprintf(stderr, "error: --registry needs a value\n"); return 2; }
            registry_name = args[++i];
        } else {
            std::fprintf(stderr, "error: unknown flag for publish: %s\n", a.c_str()); return 2;
        }
    }

    fs::path mp;
    try { mp = resolve_manifest_path(g); }
    catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(ex.what())); return 1;
    }
    auto loaded = manifest::load_manifest(mp);
    if (auto* errs = std::get_if<std::vector<diag::Diagnostic>>(&loaded)) {
        for (const auto& d : *errs) diag::print(d); return 1;
    }
    auto m = std::get<manifest::Manifest>(loaded);

    diag::status("Verifying", "package `" + m.package.name + " v" + m.package.version + "`");

    // Source-tree checksum: must match what consumers will compute, which
    // hashes a `git archive` extraction (no .git, no untracked files).
    // We materialize a temporary archive, extract it, hash, then discard.
    std::string checksum;
    {
        fs::path tmp = fs::temp_directory_path() / ("rxy_publish_" + std::to_string(::rand()));
        fs::create_directories(tmp);
        struct CleanGuard { fs::path p; ~CleanGuard(){ std::error_code ec; fs::remove_all(p, ec); } } g{tmp};

        process::Options popts_archive;
        popts_archive.cwd = m.package_root;
        popts_archive.stream_through = false;
        process::Result pa = process::run("/usr/bin/env",
            {"git", "archive", "--format=tar", "HEAD"}, popts_archive);
        if (pa.exit_code != 0) {
            diag::print(diag::Diagnostic::error("git archive failed: " + pa.stderr_data));
            return 1;
        }
        fs::path tar_path = tmp / "archive.tar";
        util::atomic_write_text_file(tar_path, pa.stdout_data);
        process::Options popts_tar;
        popts_tar.cwd = tmp;
        popts_tar.stream_through = false;
        process::Result pt = process::run("/usr/bin/tar", {"-xf", tar_path.string()}, popts_tar);
        if (pt.exit_code != 0) {
            diag::print(diag::Diagnostic::error("tar extract failed: " + pt.stderr_data));
            return 1;
        }
        std::error_code ec;
        fs::remove(tar_path, ec);
        checksum = hash::sha256_dir_tree(tmp);
    }

    // Reserved-prefix guard.
    static const std::vector<std::string> kReserved = {"rexy-", "rxy-", "std-", "core-", "alloc-"};
    std::string normalized = registry::normalize_name(m.package.name);
    for (const auto& pref : kReserved) {
        if (normalized.size() >= pref.size() && normalized.compare(0, pref.size(), pref) == 0) {
            diag::print(diag::Diagnostic::error(
                "package name `" + m.package.name + "` uses reserved prefix `" + pref + "`")
                .with_help("reserved prefixes are claimable only by the core team — see docs/registry-governance.md"));
            return 1;
        }
    }

    auto reg = registry::open_named(registry_name);

    // git-url + commit: Phase C MVP requires the package to have a git
    // working tree at HEAD with a clean working directory.
    fs::path git_exe = "/usr/bin/env";
    process::Options popts;
    popts.cwd = m.package_root;
    popts.stream_through = false;

    auto git_ok = [&](const std::vector<std::string>& argsv) -> std::optional<std::string> {
        process::Result pr = process::run(git_exe, argsv, popts);
        if (pr.exit_code != 0) return std::nullopt;
        std::string s = pr.stdout_data;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    };

    auto status = git_ok({"git", "status", "--porcelain"});
    if (!status) {
        diag::print(diag::Diagnostic::error(
            "package directory is not a git repository (rxy publish v1 requires git)")
            .with_help("run `git init && git commit -am init` first"));
        return 1;
    }
    if (!status->empty()) {
        diag::print(diag::Diagnostic::error("working tree has uncommitted changes")
            .with_help("commit or stash changes before publishing"));
        return 1;
    }
    auto commit = git_ok({"git", "rev-parse", "HEAD"});
    if (!commit) {
        diag::print(diag::Diagnostic::error("could not resolve HEAD commit"));
        return 1;
    }
    auto remote_url = git_ok({"git", "config", "--get", "remote.origin.url"});
    std::string git_url = remote_url.value_or("");
    if (git_url.empty()) {
        diag::print(diag::Diagnostic::error(
            "git remote `origin` is not set; rxy publish needs a fetch URL")
            .with_help("git remote add origin <url> first"));
        return 1;
    }

    registry::Entry entry;
    auto v = semver::parse_version(m.package.version);
    if (!v) {
        diag::print(diag::Diagnostic::error("invalid version `" + m.package.version + "`"));
        return 1;
    }
    entry.version = *v;
    entry.git_url = git_url;
    entry.commit  = *commit;
    entry.checksum = checksum;
    entry.published_at = rfc3339_now_utc();
    for (const auto& d : m.dependencies) {
        if (d.is_registry()) {
            entry.deps.push_back(d.name + " " + d.registry_version.value_or("*"));
        }
    }

    if (dry_run) {
        diag::status("Dry-run",
            "would publish " + m.package.name + " v" + m.package.version +
            " to registry `" + reg.config.name + "` (commit " + commit->substr(0, 8) +
            ", checksum " + checksum.substr(0, 16) + "...)");
        return 0;
    }

    diag::status("Publishing",
        m.package.name + " v" + m.package.version +
        " to registry `" + reg.config.name + "`");

    try {
        reg.append_entry(m.package.name, entry);
    } catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(ex.what()));
        return 1;
    }
    diag::status("Published", m.package.name + " v" + m.package.version);
    return 0;
}

int cmd_yank_or_unyank(const std::vector<std::string>& args, bool yank) {
    if (args.empty()) {
        std::fprintf(stderr, "usage: rxy %s <name>@<version> [--reason TEXT] [--severity informational|security] [--registry NAME]\n",
                     yank ? "yank" : "unyank");
        return 2;
    }
    std::string spec = args[0];
    std::string registry_name = "default";
    std::optional<std::string> reason;
    registry::YankSeverity sev = registry::YankSeverity::Informational;

    for (size_t i = 1; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "--reason") {
            if (i + 1 >= args.size()) { std::fprintf(stderr, "error: --reason needs a value\n"); return 2; }
            reason = args[++i];
        } else if (a == "--severity") {
            if (i + 1 >= args.size()) { std::fprintf(stderr, "error: --severity needs a value\n"); return 2; }
            std::string v = args[++i];
            if (v == "security") sev = registry::YankSeverity::Security;
            else if (v != "informational") {
                std::fprintf(stderr, "error: --severity must be `informational` or `security`\n");
                return 2;
            }
        } else if (a == "--registry") {
            if (i + 1 >= args.size()) { std::fprintf(stderr, "error: --registry needs a value\n"); return 2; }
            registry_name = args[++i];
        } else {
            std::fprintf(stderr, "error: unknown flag: %s\n", a.c_str()); return 2;
        }
    }

    auto at = spec.find('@');
    if (at == std::string::npos) {
        std::fprintf(stderr, "error: expected NAME@VERSION; got `%s`\n", spec.c_str());
        return 2;
    }
    std::string name = spec.substr(0, at);
    std::string ver_str = spec.substr(at + 1);
    auto v = semver::parse_version(ver_str);
    if (!v) {
        std::fprintf(stderr, "error: invalid version `%s`\n", ver_str.c_str());
        return 2;
    }

    auto reg = registry::open_named(registry_name);
    try {
        reg.set_yanked(name, *v, yank, sev, reason);
    } catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(ex.what()));
        return 1;
    }
    diag::status(yank ? "Yanked" : "Unyanked", name + " v" + ver_str);
    return 0;
}

int cmd_add(const std::vector<std::string>& args, const GlobalFlags& g) {
    if (args.empty()) {
        std::fprintf(stderr, "usage: rxy add <name>[@<constraint>] [--git URL [--tag T|--rev R|--branch B]] [--path P] [--registry NAME]\n");
        return 2;
    }

    std::string spec = args[0];
    std::optional<std::string> git_url, git_tag, git_rev, git_branch, path_arg;

    for (size_t i = 1; i < args.size(); ++i) {
        const auto& a = args[i];
        auto take = [&](const char* name) -> std::optional<std::string> {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "error: %s needs a value\n", name); return std::nullopt;
            }
            return args[++i];
        };
        if      (a == "--git")    { auto v = take("--git");    if (!v) return 2; git_url = v; }
        else if (a == "--tag")    { auto v = take("--tag");    if (!v) return 2; git_tag = v; }
        else if (a == "--rev")    { auto v = take("--rev");    if (!v) return 2; git_rev = v; }
        else if (a == "--branch") { auto v = take("--branch"); if (!v) return 2; git_branch = v; }
        else if (a == "--path")   { auto v = take("--path");   if (!v) return 2; path_arg = v; }
        else { std::fprintf(stderr, "error: unknown flag: %s\n", a.c_str()); return 2; }
    }

    std::string name = spec;
    std::string constraint;
    if (auto at = spec.find('@'); at != std::string::npos) {
        name = spec.substr(0, at);
        constraint = spec.substr(at + 1);
    }
    if (constraint.empty() && !git_url && !path_arg) constraint = "*";

    fs::path mp;
    try { mp = resolve_manifest_path(g); }
    catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(ex.what())); return 1;
    }

    std::string text = util::read_text_file(mp);
    if (text.empty() || text.back() != '\n') text.push_back('\n');

    if (text.find("[dependencies]") == std::string::npos) {
        text.append("\n[dependencies]\n");
    }
    std::ostringstream entry;
    entry << name << " = ";
    if (path_arg) {
        entry << "{ path = \"" << *path_arg << "\" }";
    } else if (git_url) {
        entry << "{ git = \"" << *git_url << "\"";
        if (git_tag)    entry << ", tag = \""    << *git_tag    << "\"";
        if (git_rev)    entry << ", rev = \""    << *git_rev    << "\"";
        if (git_branch) entry << ", branch = \"" << *git_branch << "\"";
        if (!constraint.empty() && constraint != "*") entry << ", version = \"" << constraint << "\"";
        entry << " }";
    } else {
        entry << "\"" << constraint << "\"";
    }
    entry << "\n";
    text.append(entry.str());

    util::atomic_write_text_file(mp, text);
    diag::status("Adding", name + " to [dependencies]");

    // Re-run a dry build to refresh the lockfile (no actual compile).
    auto loaded = manifest::load_manifest(mp);
    if (auto* errs = std::get_if<std::vector<diag::Diagnostic>>(&loaded)) {
        for (const auto& d : *errs) diag::print(d); return 1;
    }
    return 0;
}

int cmd_test(const std::vector<std::string>& args, const GlobalFlags& g) {
    std::optional<std::string> filter;
    bool nocapture = false;
    bool release = false;
    std::optional<std::string> target_triple;
    std::vector<std::string> child_args;
    bool seen_dashdash = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (seen_dashdash) { child_args.push_back(a); continue; }
        if (a == "--") { seen_dashdash = true; continue; }
        std::string eq_key, eq_val;
        bool has_eq = a.size() > 2 && a.substr(0, 2) == "--" && a.find('=') != std::string::npos;
        if (has_eq) {
            auto pos = a.find('=');
            eq_key = a.substr(0, pos);
            eq_val = a.substr(pos + 1);
        }
        const std::string& flag = has_eq ? eq_key : a;
        auto take = [&](const char* name) -> std::optional<std::string> {
            if (has_eq) return eq_val;
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "error: %s needs a value\n", name); return std::nullopt;
            }
            return args[++i];
        };

        if      (flag == "--nocapture") nocapture = true;
        else if (flag == "--release")   release = true;
        else if (flag == "--test") {
            auto v = take("--test"); if (!v) return 2;
            filter = *v;
        }
        else if (flag == "--target") {
            auto v = take("--target"); if (!v) return 2;
            target_triple = *v;
        }
        else if (!a.empty() && a[0] != '-' && !filter) {
            filter = a;
        }
        else {
            std::fprintf(stderr, "error: unknown test option `%s`\n", a.c_str());
            return 2;
        }
    }

    fs::path mp;
    try { mp = resolve_manifest_path(g); }
    catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(ex.what())); return 1;
    }

    auto loaded = load_with_workspace(mp);
    if (auto* errs = std::get_if<std::vector<diag::Diagnostic>>(&loaded)) {
        for (const auto& d : *errs) diag::print(d);
        return 1;
    }
    auto m = std::get<manifest::Manifest>(loaded);

    if (m.tests.empty()) {
        diag::print(diag::Diagnostic::error("no [[targets.test]] declared in this package")
            .with_help("add a [[targets.test]] entry with name + path"));
        return 1;
    }

    // Build all test targets via rexc (reuse the build pipeline by faking
    // bin targets onto a copy of the manifest).
    manifest::Manifest test_manifest = m;
    test_manifest.bins.clear();
    for (const auto& t : m.tests) {
        if (filter && t.name != *filter) continue;
        test_manifest.bins.push_back(manifest::BinTarget{t.name, t.path});
    }
    if (test_manifest.bins.empty()) {
        diag::print(diag::Diagnostic::error("no test target matched filter"));
        return 1;
    }

    build::Options bopts;
    bopts.profile_name = release ? "release" : "dev";
    bopts.target_triple = target_triple;
    bopts.quiet   = g.quiet;
    bopts.verbose = g.verbose;
    bopts.color_for_rexc = util::color_enabled_for_stderr();

    auto br = build::run(test_manifest, bopts);
    if (br.exit_code != 0) return br.exit_code;

    int passed = 0, failed = 0, skipped = 0;
    for (const auto& art : br.artifacts) {
        std::string name = art.filename().string();
        if (target_triple) {
            // Cross-compiled: skip-run, count as "skipped".
            diag::status("Skipping", name + " (cross-compiled, not runnable on host)");
            ++skipped;
            continue;
        }
        diag::status("Running", "test " + name);
        process::Options popts;
        popts.cwd = m.package_root;
        popts.stream_through = nocapture;
        process::Result pr = process::run(art, child_args, popts);
        if (pr.exit_code == 0) {
            ++passed;
        } else {
            ++failed;
            diag::print(diag::Diagnostic::error("test `" + name + "` failed (exit " +
                std::to_string(pr.exit_code) + ")"));
            if (!nocapture && !pr.stderr_data.empty()) {
                std::fprintf(stderr, "%s", pr.stderr_data.c_str());
            }
        }
    }

    if (!g.quiet) {
        std::ostringstream oss;
        oss << "test result: ";
        if (failed == 0) oss << "ok. ";
        else             oss << "FAILED. ";
        oss << passed << " passed; " << failed << " failed";
        if (skipped > 0) oss << "; " << skipped << " skipped (cross-target)";
        if (skipped > 0 && failed == 0) {
            diag::print(diag::Diagnostic::warning(oss.str()));
        } else {
            diag::status(failed == 0 ? "Finished" : "FAILED", oss.str());
        }
    }
    return failed == 0 ? 0 : 1;
}

int cmd_remove(const std::vector<std::string>& args, const GlobalFlags& g) {
    if (args.size() != 1) {
        std::fprintf(stderr, "usage: rxy remove <name>\n");
        return 2;
    }
    const std::string& name = args[0];
    fs::path mp;
    try { mp = resolve_manifest_path(g); }
    catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(ex.what())); return 1;
    }
    std::string text = util::read_text_file(mp);
    std::ostringstream out;
    bool removed = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        // Match "name = ..." at start of line (allowing a leading whitespace).
        std::string trimmed = line;
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) trimmed.erase(0, 1);
        bool matches = false;
        if (trimmed.size() > name.size() &&
            trimmed.compare(0, name.size(), name) == 0) {
            char after = trimmed[name.size()];
            if (after == ' ' || after == '=' || after == '\t') matches = true;
        }
        if (matches) { removed = true; continue; }
        out << line << "\n";
    }
    if (!removed) {
        diag::print(diag::Diagnostic::error("`" + name + "` not found in [dependencies]"));
        return 1;
    }
    util::atomic_write_text_file(mp, out.str());
    diag::status("Removed", name + " from [dependencies]");
    return 0;
}

}  // namespace

int dispatch(int argc, char** argv) {
    Parsed p = parse_argv(argc, argv);
    util::set_color_mode(p.g.color);

    // $REXY_OFFLINE=1 environment override (matches $REXY_HOME precedent).
    if (!p.g.offline) {
        if (const char* e = std::getenv("REXY_OFFLINE")) {
            std::string v = e;
            if (v == "1" || v == "true" || v == "TRUE") p.g.offline = true;
        }
    }

    if (p.error)        return 2;
    if (p.show_version) { std::printf("rxy %s\n", kVersion); return 0; }
    if (p.show_help || p.subcommand.empty()) { print_usage(); return p.show_help ? 0 : 2; }

    try {
        if (p.subcommand == "new")     return cmd_new_or_init(p.sub_args, /*is_init*/false);
        if (p.subcommand == "init")    return cmd_new_or_init(p.sub_args, /*is_init*/true);
        if (p.subcommand == "build")   return cmd_build(p.sub_args, p.g);
        if (p.subcommand == "run")     return cmd_run(p.sub_args, p.g);
        if (p.subcommand == "publish") return cmd_publish(p.sub_args, p.g);
        if (p.subcommand == "yank")    return cmd_yank_or_unyank(p.sub_args, /*yank*/true);
        if (p.subcommand == "unyank")  return cmd_yank_or_unyank(p.sub_args, /*yank*/false);
        if (p.subcommand == "add")     return cmd_add(p.sub_args, p.g);
        if (p.subcommand == "remove")  return cmd_remove(p.sub_args, p.g);
        if (p.subcommand == "test")    return cmd_test(p.sub_args, p.g);
    } catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(std::string("internal error: ") + ex.what()));
        return 101;
    }

    std::fprintf(stderr, "error: unknown command `%s`\n", p.subcommand.c_str());
    print_usage();
    return 2;
}

}  // namespace rxy::cli
