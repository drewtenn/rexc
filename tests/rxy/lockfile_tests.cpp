#include "test_main.hpp"

#include "lockfile/lockfile.hpp"
#include "manifest/manifest.hpp"
#include "util/fs.hpp"

#include <filesystem>
#include <string>

RXY_TEST("lockfile: serialize root only") {
    rxy::manifest::Manifest m;
    m.package.name    = "hello";
    m.package.version = "0.1.0";
    m.package.edition = "2026";
    auto l = rxy::lockfile::from_manifest_phase_a(m);
    auto text = rxy::lockfile::serialize(l);
    RXY_REQUIRE(text.find("name = \"hello\"") != std::string::npos);
    RXY_REQUIRE(text.find("version = \"0.1.0\"") != std::string::npos);
    RXY_REQUIRE(text.find("source = \"local\"") != std::string::npos);
    RXY_REQUIRE(text.find("# Rexy.lock") == 0);
}

RXY_TEST("lockfile: round-trip") {
    rxy::lockfile::Lockfile in;
    in.schema_version = 1;
    rxy::lockfile::LockEntry root;
    root.name = "alpha"; root.version = "1.0.0"; root.source = "local";
    rxy::lockfile::LockEntry dep;
    dep.name = "bravo"; dep.version = "0.4.2";
    dep.source = "git+https://example.test/bravo";
    dep.checksum = "sha256:deadbeef";
    dep.dependencies = {"core 0.9.0", "net 1.2.7"};
    in.packages = {root, dep};

    auto text = rxy::lockfile::serialize(in);

    auto path = std::filesystem::temp_directory_path() / "rxy_lock_roundtrip.toml";
    rxy::util::atomic_write_text_file(path, text);
    auto out = rxy::lockfile::read(path);
    RXY_REQUIRE(out.has_value());
    auto serialized_again = rxy::lockfile::serialize(*out);
    RXY_REQUIRE_EQ(text, serialized_again);
}

RXY_TEST("lockfile: deterministic order regardless of insertion") {
    rxy::lockfile::Lockfile a, b;
    a.schema_version = b.schema_version = 1;
    rxy::lockfile::LockEntry zeta{"zeta", "1.0.0", "local", std::nullopt, {}};
    rxy::lockfile::LockEntry alpha{"alpha", "0.1.0", "local", std::nullopt, {}};
    a.packages = {zeta, alpha};
    b.packages = {alpha, zeta};
    RXY_REQUIRE_EQ(rxy::lockfile::serialize(a), rxy::lockfile::serialize(b));
}

RXY_TEST("lockfile: dependencies are sorted in serialization") {
    rxy::lockfile::Lockfile l;
    rxy::lockfile::LockEntry e;
    e.name = "x"; e.version = "1.0.0"; e.source = "local";
    e.dependencies = {"zeta 1.0.0", "alpha 0.1.0", "mid 0.5.0"};
    l.packages = {e};
    auto text = rxy::lockfile::serialize(l);
    auto a = text.find("alpha");
    auto m = text.find("mid");
    auto z = text.find("zeta");
    RXY_REQUIRE(a < m && m < z);
}
