#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rxy::semver {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string pre;        // e.g. "alpha.1" — Phase C ignores in ordering
    std::string build;      // e.g. "abc123"   — never participates in ordering

    std::string to_string() const;
    bool operator==(const Version& o) const;
    bool operator<(const Version& o) const;
    bool operator<=(const Version& o) const { return *this < o || *this == o; }
    bool operator>(const Version& o)  const { return o < *this; }
    bool operator>=(const Version& o) const { return o <= *this; }
};

// Parse strictly-numeric semver "X.Y[.Z][-pre][+build]". Returns nullopt on failure.
std::optional<Version> parse_version(const std::string&);

enum class Op { Caret, Tilde, Eq, Gt, Ge, Lt, Le };

struct Term {
    Op op;
    Version v;
};

struct Constraint {
    std::vector<Term> terms;        // implicit AND
    std::string raw;                // for diagnostics

    bool matches(const Version&) const;
};

// Parses constraint syntax:
//   "0.4"        → ^0.4
//   "^0.4"       → caret
//   "~0.4.1"     → tilde
//   "=0.4.0"     → exact
//   ">=0.4, <0.6" → AND
std::optional<Constraint> parse_constraint(const std::string&);

// Returns the highest version in `available` matching `c`. Caller decides
// what to do with yanked filters etc — this is purely a numeric resolver.
std::optional<Version> resolve_highest(const Constraint& c,
                                        const std::vector<Version>& available);

}  // namespace rxy::semver
