#include "test_main.hpp"

#include "semver/semver.hpp"

#include <vector>

namespace sv = rxy::semver;

namespace {
sv::Version V(int a, int b, int c) { sv::Version v; v.major=a; v.minor=b; v.patch=c; return v; }
}

RXY_TEST("semver: parse simple versions") {
    auto v = sv::parse_version("0.4.2");
    RXY_REQUIRE(v.has_value());
    RXY_REQUIRE(v->major == 0 && v->minor == 4 && v->patch == 2);
}

RXY_TEST("semver: parse 0.4 lenient form") {
    auto v = sv::parse_version("0.4");
    RXY_REQUIRE(v.has_value());
    RXY_REQUIRE(v->major == 0 && v->minor == 4 && v->patch == 0);
}

RXY_TEST("semver: parse with prerelease + build") {
    auto v = sv::parse_version("1.2.3-alpha.1+abc");
    RXY_REQUIRE(v.has_value());
    RXY_REQUIRE(v->major == 1 && v->minor == 2 && v->patch == 3);
    RXY_REQUIRE(v->pre == "alpha.1");
    RXY_REQUIRE(v->build == "abc");
}

RXY_TEST("semver: ordering") {
    RXY_REQUIRE(V(0,1,0) < V(0,2,0));
    RXY_REQUIRE(V(0,4,9) < V(0,4,10));
    RXY_REQUIRE(V(1,0,0) > V(0,99,99));
}

RXY_TEST("semver: caret ^0.4 picks 0.4.x") {
    auto c = sv::parse_constraint("^0.4");
    RXY_REQUIRE(c.has_value());
    std::vector<sv::Version> avail = {V(0,3,0), V(0,4,0), V(0,4,5), V(0,5,0)};
    auto best = sv::resolve_highest(*c, avail);
    RXY_REQUIRE(best.has_value());
    RXY_REQUIRE(*best == V(0,4,5));
}

RXY_TEST("semver: caret ^1.2 picks 1.x") {
    auto c = sv::parse_constraint("^1.2");
    std::vector<sv::Version> avail = {V(1,2,0), V(1,3,0), V(2,0,0)};
    auto best = sv::resolve_highest(*c, avail);
    RXY_REQUIRE(best && *best == V(1,3,0));
}

RXY_TEST("semver: tilde ~0.4.1 stays in 0.4") {
    auto c = sv::parse_constraint("~0.4.1");
    std::vector<sv::Version> avail = {V(0,4,1), V(0,4,5), V(0,5,0)};
    auto best = sv::resolve_highest(*c, avail);
    RXY_REQUIRE(best && *best == V(0,4,5));
}

RXY_TEST("semver: exact =0.4.0") {
    auto c = sv::parse_constraint("=0.4.0");
    std::vector<sv::Version> avail = {V(0,4,0), V(0,4,1)};
    auto best = sv::resolve_highest(*c, avail);
    RXY_REQUIRE(best && *best == V(0,4,0));
}

RXY_TEST("semver: AND constraints >=0.4, <0.6") {
    auto c = sv::parse_constraint(">=0.4, <0.6");
    std::vector<sv::Version> avail = {V(0,3,0), V(0,4,0), V(0,5,5), V(0,6,0)};
    auto best = sv::resolve_highest(*c, avail);
    RXY_REQUIRE(best && *best == V(0,5,5));
}

RXY_TEST("semver: bare 0.4 is sugar for ^0.4") {
    auto c = sv::parse_constraint("0.4");
    std::vector<sv::Version> avail = {V(0,4,0), V(0,4,7), V(0,5,0)};
    auto best = sv::resolve_highest(*c, avail);
    RXY_REQUIRE(best && *best == V(0,4,7));
}

RXY_TEST("semver: no match returns nullopt") {
    auto c = sv::parse_constraint("^9.0");
    std::vector<sv::Version> avail = {V(0,1,0)};
    auto best = sv::resolve_highest(*c, avail);
    RXY_REQUIRE(!best.has_value());
}
