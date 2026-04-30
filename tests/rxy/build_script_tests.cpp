#include "test_main.hpp"

#include "build_script/build_script.hpp"
#include "manifest/manifest.hpp"

#include <filesystem>

namespace bs = rxy::build_script;

RXY_TEST("build_script: parse_stdout extracts each directive type") {
    std::string s =
        "Some informational line\n"
        "rxy:rerun-if-changed=src/foo.h\n"
        "rxy:rerun-if-changed=src/bar.h\n"
        "rxy:rerun-if-env-changed=SSL_CERT_FILE\n"
        "rxy:rxy-search-path=/tmp/out/gen\n"
        "rxy:env=MY_VAR=value\n"
        "rxy:warning=using legacy openssl\n"
        "rxy:cfg=use_glibc\n"
        "rxy:link-lib=ssl\n"
        "rxy:link-search=/opt/lib\n"
        "ignored\n";
    auto d = bs::parse_stdout(s);
    RXY_REQUIRE(d.rerun_if_changed.size() == 2);
    RXY_REQUIRE(d.rerun_if_env_changed.size() == 1);
    RXY_REQUIRE(d.rxy_search_paths.size() == 1);
    RXY_REQUIRE(d.env_overlay.size() == 1);
    RXY_REQUIRE(d.env_overlay.at("MY_VAR") == "value");
    RXY_REQUIRE(d.warnings.size() == 1);
    RXY_REQUIRE(d.cfgs.size() == 1);
    RXY_REQUIRE(d.link_libs.size() == 1);
    RXY_REQUIRE(d.link_searches.size() == 1);
}

RXY_TEST("build_script: parse_stdout ignores malformed lines") {
    std::string s =
        "rxy:no-equals\n"
        "rxy:\n"
        "rxy:env=novalue\n"           // no = inside value
        "rxy:link-lib=valid\n";
    auto d = bs::parse_stdout(s);
    RXY_REQUIRE(d.link_libs.size() == 1);
    RXY_REQUIRE(d.env_overlay.empty());
}

RXY_TEST("build_script: cache_hash differs on each input") {
    rxy::manifest::Manifest m;
    m.package.name = "foo"; m.package.version = "0.1.0";
    bs::RunOptions o;
    o.profile_name = "dev";
    o.host_triple  = "arm64-apple-darwin";
    o.target_triple = "arm64-apple-darwin";
    o.rexc_version = "0.1";

    std::string h1 = bs::cache_hash(m, o, "fn main() -> i32 { return 0; }");
    std::string h2 = bs::cache_hash(m, o, "fn main() -> i32 { return 1; }");
    RXY_REQUIRE(h1 != h2);

    auto o2 = o; o2.profile_name = "release";
    std::string h3 = bs::cache_hash(m, o2, "fn main() -> i32 { return 0; }");
    RXY_REQUIRE(h1 != h3);

    auto o3 = o; o3.target_triple = "x86_64-linux";
    std::string h4 = bs::cache_hash(m, o3, "fn main() -> i32 { return 0; }");
    RXY_REQUIRE(h1 != h4);
}

RXY_TEST("build_script: disallowed_transitive_scripts identifies blocked deps") {
    rxy::manifest::Manifest root;
    root.package.name = "app";
    rxy::manifest::DependencySpec d;
    d.name = "direct-dep";
    d.path = std::filesystem::path("../direct-dep");
    root.dependencies.push_back(d);
    rxy::manifest::BuildSection bsec;
    bsec.allow_scripts = {"transitive-allowed"};
    root.build = bsec;

    rxy::manifest::Manifest direct;
    direct.package.name = "direct-dep";
    rxy::manifest::BuildSection direct_bs;
    direct_bs.script = std::filesystem::path("build.rx");
    direct.build = direct_bs;

    rxy::manifest::Manifest trans_allowed;
    trans_allowed.package.name = "transitive-allowed";
    rxy::manifest::BuildSection trans_allowed_bs;
    trans_allowed_bs.script = std::filesystem::path("build.rx");
    trans_allowed.build = trans_allowed_bs;

    rxy::manifest::Manifest trans_blocked;
    trans_blocked.package.name = "transitive-blocked";
    rxy::manifest::BuildSection trans_blocked_bs;
    trans_blocked_bs.script = std::filesystem::path("build.rx");
    trans_blocked.build = trans_blocked_bs;

    auto blocked = bs::disallowed_transitive_scripts(root, {direct, trans_allowed, trans_blocked});
    RXY_REQUIRE(blocked.size() == 1);
    RXY_REQUIRE(blocked[0] == "transitive-blocked");
}

RXY_TEST("build_script: dep with no build script is never blocked") {
    rxy::manifest::Manifest root;
    root.package.name = "app";

    rxy::manifest::Manifest dep;
    dep.package.name = "no-script";
    // dep.build = nullopt
    auto blocked = bs::disallowed_transitive_scripts(root, {dep});
    RXY_REQUIRE(blocked.empty());
}
