#include "test_main.hpp"

#include "registry/registry.hpp"
#include "semver/semver.hpp"
#include "util/fs.hpp"

#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
// Redirect REXY_HOME for the duration of one test.
struct ScopedHome {
    std::string saved;
    bool had = false;
    fs::path dir;
    explicit ScopedHome() {
        const char* p = std::getenv("REXY_HOME");
        if (p) { saved = p; had = true; }
        // Use a per-process unique path so successive `ctest` runs don't
        // see each other's state. PID + monotonic counter is enough.
        static int counter = 0;
        dir = fs::temp_directory_path() /
              ("rxy_reg_" + std::to_string(::getpid()) + "_" + std::to_string(++counter));
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        ::setenv("REXY_HOME", dir.string().c_str(), 1);
    }
    ~ScopedHome() {
        if (had) ::setenv("REXY_HOME", saved.c_str(), 1);
        else      ::unsetenv("REXY_HOME");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};
}

RXY_TEST("registry: normalize_name handles case + underscore") {
    RXY_REQUIRE(rxy::registry::normalize_name("Foo_Bar") == "foo-bar");
    RXY_REQUIRE(rxy::registry::normalize_name("ALL_CAPS") == "all-caps");
}

RXY_TEST("registry: bucket_prefix follows Cargo conventions") {
    RXY_REQUIRE(rxy::registry::bucket_prefix("a")    == "1/a");
    RXY_REQUIRE(rxy::registry::bucket_prefix("ab")   == "2/ab");
    RXY_REQUIRE(rxy::registry::bucket_prefix("abc")  == "3/a/bc");
    RXY_REQUIRE(rxy::registry::bucket_prefix("util") == "ut/il");
    RXY_REQUIRE(rxy::registry::bucket_prefix("longer-name") == "lo/ng");
}

RXY_TEST("registry: append + lookup round-trip") {
    ScopedHome home;
    auto reg = rxy::registry::open_default();
    rxy::registry::Entry e;
    e.version = *rxy::semver::parse_version("0.4.0");
    e.git_url = "https://example.test/util.rx";
    e.commit  = "abcdef0011223344556677889900aabbccddeeff";
    e.checksum = "sha256:cafebabe";
    e.deps = {"core ^0.9"};
    e.published_at = "2026-04-30T03:14:15Z";
    reg.append_entry("util", e);

    auto info = reg.lookup("util");
    RXY_REQUIRE(info.has_value());
    RXY_REQUIRE(info->name == "util");
    RXY_REQUIRE(info->entries.size() == 1);
    RXY_REQUIRE(info->entries[0].version == e.version);
    RXY_REQUIRE(info->entries[0].commit == e.commit);
    RXY_REQUIRE(info->entries[0].deps == e.deps);
}

RXY_TEST("registry: append rejects duplicate version") {
    ScopedHome home;
    auto reg = rxy::registry::open_default();
    rxy::registry::Entry e;
    e.version = *rxy::semver::parse_version("0.1.0");
    e.git_url = "u"; e.commit = "c"; e.checksum = "sha256:x";
    reg.append_entry("dupe", e);
    bool threw = false;
    try { reg.append_entry("dupe", e); }
    catch (const std::exception& ex) { threw = std::string(ex.what()).find("already published") != std::string::npos; }
    RXY_REQUIRE(threw);
}

RXY_TEST("registry: yank flips the flag and round-trips") {
    ScopedHome home;
    auto reg = rxy::registry::open_default();
    auto v = *rxy::semver::parse_version("0.2.0");
    rxy::registry::Entry e;
    e.version = v; e.git_url = "u"; e.commit = "c"; e.checksum = "sha256:x";
    reg.append_entry("foo", e);

    reg.set_yanked("foo", v, true,
                    rxy::registry::YankSeverity::Security,
                    std::string("CVE-2026-12345"));
    auto info = reg.lookup("foo");
    RXY_REQUIRE(info && !info->entries.empty());
    RXY_REQUIRE(info->entries[0].yanked == true);
    RXY_REQUIRE(info->entries[0].yank_severity == rxy::registry::YankSeverity::Security);
    RXY_REQUIRE(info->entries[0].yank_reason && *info->entries[0].yank_reason == "CVE-2026-12345");

    reg.set_yanked("foo", v, false);
    info = reg.lookup("foo");
    RXY_REQUIRE(info->entries[0].yanked == false);
}
