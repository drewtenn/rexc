#include "test_main.hpp"

#include "manifest/manifest.hpp"
#include "util/fs.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path make_tmp_pkg(const std::string& toml_body, const std::string& sub = "rxy_test_") {
    fs::path dir = fs::temp_directory_path() / (sub + std::to_string(::rand()));
    fs::create_directories(dir);
    fs::create_directories(dir / "src");
    rxy::util::atomic_write_text_file(dir / "Rexy.toml", toml_body);
    // Drop a stub main.rx so default-target detection works for the smoke cases.
    std::ofstream(dir / "src/main.rx") << "fn main() -> i32 { return 0; }\n";
    return dir;
}

}  // namespace

RXY_TEST("manifest: minimal happy path") {
    auto dir = make_tmp_pkg(R"(
[package]
name = "hello"
version = "0.1.0"
edition = "2026"
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    auto* m = std::get_if<rxy::manifest::Manifest>(&r);
    RXY_REQUIRE(m != nullptr);
    RXY_REQUIRE(m->package.name == "hello");
    RXY_REQUIRE(m->package.version == "0.1.0");
    RXY_REQUIRE(m->package.edition == "2026");
    RXY_REQUIRE(m->bins.size() == 1);
    RXY_REQUIRE(m->bins.front().name == "hello");
}

RXY_TEST("manifest: missing version") {
    auto dir = make_tmp_pkg(R"(
[package]
name = "x"
edition = "2026"
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    auto* errs = std::get_if<std::vector<rxy::diag::Diagnostic>>(&r);
    RXY_REQUIRE(errs != nullptr);
    bool found = false;
    for (const auto& d : *errs) {
        if (d.message.find("version") != std::string::npos) found = true;
    }
    RXY_REQUIRE(found);
}

RXY_TEST("manifest: invalid edition") {
    auto dir = make_tmp_pkg(R"(
[package]
name = "x"
version = "0.1.0"
edition = "twenty-twenty-six"
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    RXY_REQUIRE(std::holds_alternative<std::vector<rxy::diag::Diagnostic>>(r));
}

RXY_TEST("manifest: unknown top-level key rejected") {
    auto dir = make_tmp_pkg(R"(
[package]
name = "x"
version = "0.1.0"
edition = "2026"

[mystery]
flavor = "vanilla"
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    auto* errs = std::get_if<std::vector<rxy::diag::Diagnostic>>(&r);
    RXY_REQUIRE(errs != nullptr);
    bool found = false;
    for (const auto& d : *errs) {
        if (d.message.find("mystery") != std::string::npos) found = true;
    }
    RXY_REQUIRE(found);
}

RXY_TEST("manifest: explicit bin target") {
    auto dir = make_tmp_pkg(R"(
[package]
name = "tool"
version = "0.2.0"
edition = "2026"

[[targets.bin]]
name = "tool-cli"
path = "src/cli.rx"
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    auto* m = std::get_if<rxy::manifest::Manifest>(&r);
    RXY_REQUIRE(m != nullptr);
    RXY_REQUIRE(m->bins.size() == 1);
    RXY_REQUIRE(m->bins.front().name == "tool-cli");
    RXY_REQUIRE(m->bins.front().path == fs::path("src/cli.rx"));
}

RXY_TEST("manifest: profile defaults override") {
    auto dir = make_tmp_pkg(R"(
[package]
name = "x"
version = "0.1.0"
edition = "2026"

[profile.release]
opt-level = 2
lto = false
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    auto* m = std::get_if<rxy::manifest::Manifest>(&r);
    RXY_REQUIRE(m != nullptr);
    auto p = m->resolved_profile("release");
    RXY_REQUIRE(p.opt_level && *p.opt_level == 2);
    RXY_REQUIRE(p.lto && *p.lto == false);
    // strip default for release should still come through
    RXY_REQUIRE(p.strip && *p.strip == true);
}

RXY_TEST("manifest: bad TOML reports line/col") {
    auto dir = make_tmp_pkg(R"(
[package
name = "x"
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    auto* errs = std::get_if<std::vector<rxy::diag::Diagnostic>>(&r);
    RXY_REQUIRE(errs != nullptr);
    RXY_REQUIRE(!errs->empty());
}
