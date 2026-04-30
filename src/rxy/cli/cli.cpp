#include "cli/cli.hpp"

#include "build/build.hpp"
#include "diag/diag.hpp"
#include "lockfile/lockfile.hpp"
#include "manifest/manifest.hpp"
#include "process/process.hpp"
#include "util/fs.hpp"
#include "util/tty.hpp"

#include <cstdio>
#include <cstdlib>
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
        else if (flag == "--profile") {
            auto v = take("--profile"); if (!v) return 2;
            bopts.profile_name = *v;
        }
        else if (flag == "--bin") {
            auto v = take("--bin"); if (!v) return 2;
            bopts.bin = *v;
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

    auto loaded = manifest::load_manifest(mp);
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

    auto loaded = manifest::load_manifest(mp);
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

}  // namespace

int dispatch(int argc, char** argv) {
    Parsed p = parse_argv(argc, argv);
    util::set_color_mode(p.g.color);

    if (p.error)        return 2;
    if (p.show_version) { std::printf("rxy %s\n", kVersion); return 0; }
    if (p.show_help || p.subcommand.empty()) { print_usage(); return p.show_help ? 0 : 2; }

    try {
        if (p.subcommand == "new")   return cmd_new_or_init(p.sub_args, /*is_init*/false);
        if (p.subcommand == "init")  return cmd_new_or_init(p.sub_args, /*is_init*/true);
        if (p.subcommand == "build") return cmd_build(p.sub_args, p.g);
        if (p.subcommand == "run")   return cmd_run(p.sub_args, p.g);
    } catch (const std::exception& ex) {
        diag::print(diag::Diagnostic::error(std::string("internal error: ") + ex.what()));
        return 101;
    }

    std::fprintf(stderr, "error: unknown command `%s`\n", p.subcommand.c_str());
    print_usage();
    return 2;
}

}  // namespace rxy::cli
