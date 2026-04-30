#include "test_main.hpp"

#include "manifest/manifest.hpp"
#include "source/source.hpp"
#include "util/fs.hpp"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

RXY_TEST("offline: resolve std with bundled-compatible constraint succeeds") {
    rxy::manifest::DependencySpec dep;
    dep.name = "std";
    dep.registry_version = "0.1";    // matches RXY_BUNDLED_STDLIB_VERSION = "0.1.0"
    rxy::source::ResolveOptions opts;
    opts.offline = true;             // even offline — bundled stdlib has no network
    auto r = rxy::source::resolve(dep, fs::current_path(), opts);
    RXY_REQUIRE(r.name == "std");
    RXY_REQUIRE(r.source_token == "bundled+rexc");
    RXY_REQUIRE(r.version == "0.1.0");
}

RXY_TEST("offline: resolve std with incompatible pin fails clearly") {
    rxy::manifest::DependencySpec dep;
    dep.name = "std";
    dep.registry_version = "^99.0";
    rxy::source::ResolveOptions opts;
    bool threw = false;
    std::string what;
    try {
        rxy::source::resolve(dep, fs::current_path(), opts);
    } catch (const std::exception& ex) {
        threw = true;
        what = ex.what();
    }
    RXY_REQUIRE(threw);
    RXY_REQUIRE(what.find("does not accept") != std::string::npos);
    RXY_REQUIRE(what.find("bundled stdlib") != std::string::npos);
}

RXY_TEST("offline: git source resolve refuses to clone when no cache") {
    rxy::manifest::DependencySpec dep;
    dep.name = "phantom-pkg";
    dep.git_url = "file:///nonexistent/path/that/never/exists.git";
    dep.git_rev = "deadbeef";
    rxy::source::ResolveOptions opts;
    opts.offline = true;
    bool threw = false;
    std::string what;
    try {
        rxy::source::resolve(dep, fs::current_path(), opts);
    } catch (const std::exception& ex) {
        threw = true;
        what = ex.what();
    }
    RXY_REQUIRE(threw);
    RXY_REQUIRE(what.find("offline mode") != std::string::npos);
    RXY_REQUIRE(what.find("no cached clone") != std::string::npos);
}
