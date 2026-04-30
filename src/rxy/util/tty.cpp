#include "util/tty.hpp"

#include <unistd.h>

namespace rxy::util {

namespace ansi {
const char* RESET  = "\x1b[0m";
const char* BOLD   = "\x1b[1m";
const char* RED    = "\x1b[31;1m";
const char* GREEN  = "\x1b[32;1m";
const char* YELLOW = "\x1b[33;1m";
const char* BLUE   = "\x1b[34;1m";
const char* CYAN   = "\x1b[36;1m";
const char* DIM    = "\x1b[2m";
}  // namespace ansi

namespace {
ColorMode g_mode = ColorMode::Auto;
}

bool stderr_is_tty() { return ::isatty(STDERR_FILENO) != 0; }
bool stdout_is_tty() { return ::isatty(STDOUT_FILENO) != 0; }

void set_color_mode(ColorMode m) { g_mode = m; }
ColorMode color_mode() { return g_mode; }

bool color_enabled_for_stderr() {
    switch (g_mode) {
        case ColorMode::Always: return true;
        case ColorMode::Never:  return false;
        case ColorMode::Auto:   return stderr_is_tty();
    }
    return false;
}

bool color_enabled_for_stdout() {
    switch (g_mode) {
        case ColorMode::Always: return true;
        case ColorMode::Never:  return false;
        case ColorMode::Auto:   return stdout_is_tty();
    }
    return false;
}

}  // namespace rxy::util
