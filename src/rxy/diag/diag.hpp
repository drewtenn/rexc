#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rxy::diag {

enum class Severity { Error, Warning, Note, Help };

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string message;
    std::optional<std::filesystem::path> file;
    std::optional<int> line;     // 1-based
    std::optional<int> col;      // 1-based
    std::optional<std::string> source_line;     // verbatim source line for snippet
    std::optional<int> span_len;                // length of caret span
    std::vector<std::string> notes;
    std::optional<std::string> help;

    static Diagnostic error(std::string msg);
    static Diagnostic warning(std::string msg);

    Diagnostic& at(std::filesystem::path f, int l, int c);
    Diagnostic& with_source(std::string line, int span);
    Diagnostic& note(std::string n);
    Diagnostic& with_help(std::string h);
};

// Print a single diagnostic to stderr with Cargo-style formatting,
// honoring the active color mode and the 8-line cap.
void print(const Diagnostic&);

// Print a status line ("Compiling foo v0.1.0 (path/to/foo)") to stderr.
// `verb` is right-aligned to a 12-char column for visual consistency.
void status(const std::string& verb, const std::string& detail);

// Print a "Finished" summary line.
void finished_summary(const std::string& profile_label, double seconds);

}  // namespace rxy::diag
