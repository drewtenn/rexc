#include "test_main.hpp"

#include "manifest/manifest.hpp"
#include "lockfile/lockfile.hpp"
#include "util/fs.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
fs::path make_pkg(const std::string& body, const std::string& sub = "rxy_dep_") {
    fs::path dir = fs::temp_directory_path() / (sub + std::to_string(::rand()));
    fs::create_directories(dir / "src");
    rxy::util::atomic_write_text_file(dir / "Rexy.toml", body);
    std::ofstream(dir / "src/main.rx") << "fn main() -> i32 { return 0; }\n";
    return dir;
}
}

RXY_TEST("deps: path dependency parsed correctly") {
    auto dir = make_pkg(R"(
[package]
name = "app"
version = "0.1.0"
edition = "2026"

[dependencies]
util = { path = "../util" }
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    auto* m = std::get_if<rxy::manifest::Manifest>(&r);
    RXY_REQUIRE(m != nullptr);
    RXY_REQUIRE(m->dependencies.size() == 1);
    RXY_REQUIRE(m->dependencies[0].name == "util");
    RXY_REQUIRE(m->dependencies[0].is_path());
    RXY_REQUIRE(m->dependencies[0].path->string() == "../util");
}

RXY_TEST("deps: git dependency with tag parsed") {
    auto dir = make_pkg(R"(
[package]
name = "app"
version = "0.1.0"
edition = "2026"

[dependencies]
util = { git = "https://example.test/util.rx", tag = "v0.4.0" }
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    auto* m = std::get_if<rxy::manifest::Manifest>(&r);
    RXY_REQUIRE(m != nullptr);
    RXY_REQUIRE(m->dependencies.size() == 1);
    const auto& d = m->dependencies[0];
    RXY_REQUIRE(d.is_git());
    RXY_REQUIRE(d.git_url && *d.git_url == "https://example.test/util.rx");
    RXY_REQUIRE(d.git_tag && *d.git_tag == "v0.4.0");
}

RXY_TEST("deps: registry-only string version is parsed but not yet supported") {
    auto dir = make_pkg(R"(
[package]
name = "app"
version = "0.1.0"
edition = "2026"

[dependencies]
util = "0.4"
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    auto* m = std::get_if<rxy::manifest::Manifest>(&r);
    RXY_REQUIRE(m != nullptr);
    RXY_REQUIRE(m->dependencies.size() == 1);
    RXY_REQUIRE(m->dependencies[0].is_registry());
    RXY_REQUIRE(m->dependencies[0].registry_version == "0.4");
}

RXY_TEST("deps: mixed path + git is rejected") {
    auto dir = make_pkg(R"(
[package]
name = "app"
version = "0.1.0"
edition = "2026"

[dependencies]
util = { path = "../u", git = "https://x.test/u.rx" }
)");
    auto r = rxy::manifest::load_manifest(dir / "Rexy.toml");
    RXY_REQUIRE(std::holds_alternative<std::vector<rxy::diag::Diagnostic>>(r));
}

RXY_TEST("lockfile: drift detection flags changed commit") {
    rxy::lockfile::Lockfile prev;
    prev.schema_version = 1;
    rxy::lockfile::LockEntry e;
    e.name = "util"; e.version = "0.4.0";
    e.source = "git+https://x.test/util.rx?tag=v0.4.0";
    e.commit = "1111111111111111111111111111111111111111";
    e.checksum = "sha256:aaaa";
    prev.packages = {e};

    rxy::lockfile::Lockfile fresh = prev;
    fresh.packages[0].commit   = "2222222222222222222222222222222222222222";
    fresh.packages[0].checksum = "sha256:bbbb";

    auto drift = rxy::lockfile::detect_drift(prev, fresh);
    RXY_REQUIRE(drift.has_value());
    RXY_REQUIRE(drift->find("force-pushed") != std::string::npos);
}

RXY_TEST("lockfile: no drift when entries match") {
    rxy::lockfile::Lockfile prev, fresh;
    prev.schema_version = fresh.schema_version = 1;
    rxy::lockfile::LockEntry e;
    e.name = "util"; e.version = "0.4.0";
    e.source = "git+https://x.test/util.rx?tag=v0.4.0";
    e.commit = "abcdef0011223344556677889900aabbccddeeff";
    e.checksum = "sha256:cafe";
    prev.packages = fresh.packages = {e};
    auto drift = rxy::lockfile::detect_drift(prev, fresh);
    RXY_REQUIRE(!drift.has_value());
}
