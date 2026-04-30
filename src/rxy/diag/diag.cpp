#include "diag/diag.hpp"

#include "util/tty.hpp"

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace rxy::diag {

namespace {

// Strip C0 control characters (ESC, BEL, etc.) from user-controlled strings
// before printing to stderr. Without this, an adversarial manifest field
// could embed terminal-control escapes ("\x1b[2J", "\x1b]0;owned\x07", etc.)
// that rewrite the user's terminal title, clear the screen, or replay forged
// success lines in CI logs. Whitelisted: \t (0x09) and \n (0x0a) only.
std::string strip_ansi(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 && u != '\t' && u != '\n') continue;
        if (u == 0x7f) continue;          // DEL
        out.push_back(c);
    }
    return out;
}

const char* sev_prefix(Severity s) {
    switch (s) {
        case Severity::Error:   return "error";
        case Severity::Warning: return "warning";
        case Severity::Note:    return "note";
        case Severity::Help:    return "help";
    }
    return "";
}

const char* sev_color(Severity s) {
    using namespace util::ansi;
    switch (s) {
        case Severity::Error:   return RED;
        case Severity::Warning: return YELLOW;
        case Severity::Note:    return BLUE;
        case Severity::Help:    return CYAN;
    }
    return RESET;
}
}  // namespace

Diagnostic Diagnostic::error(std::string msg) {
    Diagnostic d;
    d.severity = Severity::Error;
    d.message  = std::move(msg);
    return d;
}

Diagnostic Diagnostic::warning(std::string msg) {
    Diagnostic d;
    d.severity = Severity::Warning;
    d.message  = std::move(msg);
    return d;
}

Diagnostic& Diagnostic::at(std::filesystem::path f, int l, int c) {
    file = std::move(f);
    line = l;
    col  = c;
    return *this;
}

Diagnostic& Diagnostic::with_source(std::string l, int span) {
    source_line = std::move(l);
    span_len    = span > 0 ? span : 1;
    return *this;
}

Diagnostic& Diagnostic::note(std::string n) {
    notes.push_back(std::move(n));
    return *this;
}

Diagnostic& Diagnostic::with_help(std::string h) {
    help = std::move(h);
    return *this;
}

void print(const Diagnostic& d) {
    bool color = util::color_enabled_for_stderr();
    auto c = [color](const char* s) { return color ? s : ""; };
    using namespace util::ansi;

    int line_count = 0;
    auto cap_ok = [&]() { return line_count < 8; };

    // header: "<sev>: <message>"
    std::string safe_message = strip_ansi(d.message);
    std::fprintf(stderr, "%s%s%s%s: %s\n",
                 c(sev_color(d.severity)),
                 c(BOLD),
                 sev_prefix(d.severity),
                 c(RESET),
                 safe_message.c_str());
    ++line_count;

    if (d.file && d.line && d.col && cap_ok()) {
        std::string display_path = strip_ansi(d.file->string());
        std::fprintf(stderr, "  %s-->%s %s:%d:%d\n",
                     c(CYAN), c(RESET),
                     display_path.c_str(), *d.line, *d.col);
        ++line_count;
    }

    if (d.source_line && d.line && cap_ok()) {
        // 4-char line-number gutter.
        std::fprintf(stderr, "   %s|%s\n", c(BLUE), c(RESET));
        ++line_count;
        if (cap_ok()) {
            std::string safe_source = strip_ansi(*d.source_line);
            std::fprintf(stderr, "%s%4d |%s %s\n",
                         c(BLUE), *d.line, c(RESET), safe_source.c_str());
            ++line_count;
        }
        if (cap_ok() && d.col && d.span_len) {
            std::string pad(static_cast<size_t>(*d.col - 1), ' ');
            std::string carets(static_cast<size_t>(*d.span_len), '^');
            std::fprintf(stderr, "   %s|%s %s%s%s%s\n",
                         c(BLUE), c(RESET),
                         pad.c_str(),
                         c(sev_color(d.severity)),
                         carets.c_str(),
                         c(RESET));
            ++line_count;
        }
    }

    for (const auto& n : d.notes) {
        if (!cap_ok()) break;
        std::string safe_note = strip_ansi(n);
        std::fprintf(stderr, "  %s%snote%s: %s\n",
                     c(BLUE), c(BOLD), c(RESET), safe_note.c_str());
        ++line_count;
    }

    if (d.help && cap_ok()) {
        std::string safe_help = strip_ansi(*d.help);
        std::fprintf(stderr, "  %s%shelp%s: %s\n",
                     c(CYAN), c(BOLD), c(RESET), safe_help.c_str());
        ++line_count;
    }
    std::fflush(stderr);
}

void status(const std::string& verb, const std::string& detail) {
    bool color = util::color_enabled_for_stderr();
    using namespace util::ansi;
    if (color) {
        std::fprintf(stderr, "%s%s%12s%s %s\n",
                     GREEN, BOLD, verb.c_str(), RESET, detail.c_str());
    } else {
        std::fprintf(stderr, "%12s %s\n", verb.c_str(), detail.c_str());
    }
    std::fflush(stderr);
}

void finished_summary(const std::string& profile_label, double seconds) {
    std::ostringstream oss;
    oss << "`" << profile_label << "` profile target(s) in "
        << std::fixed << std::setprecision(2) << seconds << "s";
    status("Finished", oss.str());
}

}  // namespace rxy::diag
