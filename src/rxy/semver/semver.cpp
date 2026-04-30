#include "semver/semver.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace rxy::semver {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}

std::optional<int> parse_int(const std::string& s) {
    if (s.empty()) return std::nullopt;
    for (char c : s) if (!is_digit(c)) return std::nullopt;
    try { return std::stoi(s); } catch (...) { return std::nullopt; }
}

// Returns (left_part, right_part) at first occurrence of `delim`. If not
// present, returns (s, "").
std::pair<std::string, std::string> split_first(const std::string& s, char delim) {
    auto pos = s.find(delim);
    if (pos == std::string::npos) return {s, ""};
    return {s.substr(0, pos), s.substr(pos + 1)};
}

}  // namespace

std::string Version::to_string() const {
    std::ostringstream oss;
    oss << major << "." << minor << "." << patch;
    if (!pre.empty()) oss << "-" << pre;
    if (!build.empty()) oss << "+" << build;
    return oss.str();
}

bool Version::operator==(const Version& o) const {
    return major == o.major && minor == o.minor && patch == o.patch && pre == o.pre;
}

bool Version::operator<(const Version& o) const {
    if (major != o.major) return major < o.major;
    if (minor != o.minor) return minor < o.minor;
    if (patch != o.patch) return patch < o.patch;
    // Pre-release versions are LESS than non-pre-release.
    if (pre.empty() && !o.pre.empty()) return false;
    if (!pre.empty() && o.pre.empty()) return true;
    return pre < o.pre;
}

std::optional<Version> parse_version(const std::string& in) {
    std::string s = trim(in);
    if (s.empty()) return std::nullopt;

    auto [head, build] = split_first(s, '+');
    auto [core, pre]   = split_first(head, '-');

    Version v;
    v.pre   = pre;
    v.build = build;

    auto first_dot  = core.find('.');
    if (first_dot == std::string::npos) {
        // "1" → 1.0.0 — accept lenient forms in constraints
        auto mj = parse_int(core);
        if (!mj) return std::nullopt;
        v.major = *mj;
        return v;
    }
    auto second_dot = core.find('.', first_dot + 1);
    auto mj = parse_int(core.substr(0, first_dot));
    if (!mj) return std::nullopt;
    v.major = *mj;
    if (second_dot == std::string::npos) {
        auto mn = parse_int(core.substr(first_dot + 1));
        if (!mn) return std::nullopt;
        v.minor = *mn;
        return v;
    }
    auto mn = parse_int(core.substr(first_dot + 1, second_dot - first_dot - 1));
    auto pt = parse_int(core.substr(second_dot + 1));
    if (!mn || !pt) return std::nullopt;
    v.minor = *mn;
    v.patch = *pt;
    return v;
}

namespace {

bool match_op(Op op, const Version& candidate, const Version& target) {
    switch (op) {
        case Op::Eq:    return candidate == target;
        case Op::Gt:    return candidate >  target;
        case Op::Ge:    return candidate >= target;
        case Op::Lt:    return candidate <  target;
        case Op::Le:    return candidate <= target;
        case Op::Caret: {
            // ^A.B.C: match >= A.B.C, < {next-most-significant-non-zero+1}.0.0|0
            if (candidate < target) return false;
            if (target.major != 0) {
                Version upper{target.major + 1, 0, 0, "", ""};
                return candidate < upper;
            }
            if (target.minor != 0) {
                Version upper{0, target.minor + 1, 0, "", ""};
                return candidate < upper;
            }
            Version upper{0, 0, target.patch + 1, "", ""};
            return candidate < upper;
        }
        case Op::Tilde: {
            // ~A.B.C: >= A.B.C, < A.(B+1).0
            if (candidate < target) return false;
            Version upper{target.major, target.minor + 1, 0, "", ""};
            return candidate < upper;
        }
    }
    return false;
}

}  // namespace

bool Constraint::matches(const Version& v) const {
    for (const auto& t : terms) if (!match_op(t.op, v, t.v)) return false;
    return true;
}

std::optional<Constraint> parse_constraint(const std::string& in) {
    Constraint c;
    c.raw = in;
    std::string s = trim(in);
    if (s.empty()) return std::nullopt;

    // Split on commas → AND of terms.
    std::vector<std::string> parts;
    {
        std::string cur;
        for (char ch : s) {
            if (ch == ',') { parts.push_back(trim(cur)); cur.clear(); }
            else           { cur.push_back(ch); }
        }
        if (!cur.empty()) parts.push_back(trim(cur));
    }

    for (auto& p : parts) {
        if (p.empty()) continue;
        Term t;
        // Detect operator.
        auto starts_with = [&](const std::string& pref) {
            return p.size() >= pref.size() &&
                   p.compare(0, pref.size(), pref) == 0;
        };
        std::string body;
        if      (starts_with(">="))  { t.op = Op::Ge;    body = trim(p.substr(2)); }
        else if (starts_with("<="))  { t.op = Op::Le;    body = trim(p.substr(2)); }
        else if (starts_with(">"))   { t.op = Op::Gt;    body = trim(p.substr(1)); }
        else if (starts_with("<"))   { t.op = Op::Lt;    body = trim(p.substr(1)); }
        else if (starts_with("="))   { t.op = Op::Eq;    body = trim(p.substr(1)); }
        else if (starts_with("^"))   { t.op = Op::Caret; body = trim(p.substr(1)); }
        else if (starts_with("~"))   { t.op = Op::Tilde; body = trim(p.substr(1)); }
        else                          { t.op = Op::Caret; body = p; }
        auto v = parse_version(body);
        if (!v) return std::nullopt;
        t.v = *v;
        c.terms.push_back(t);
    }
    if (c.terms.empty()) return std::nullopt;
    return c;
}

std::optional<Version> resolve_highest(const Constraint& c,
                                        const std::vector<Version>& available) {
    std::optional<Version> best;
    for (const auto& v : available) {
        if (!c.matches(v)) continue;
        if (!best || *best < v) best = v;
    }
    return best;
}

}  // namespace rxy::semver
