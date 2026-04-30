#pragma once

#include <string>

namespace rxy::util {

enum class ColorMode { Auto, Always, Never };

bool stderr_is_tty();
bool stdout_is_tty();

void set_color_mode(ColorMode);
ColorMode color_mode();

bool color_enabled_for_stderr();
bool color_enabled_for_stdout();

namespace ansi {
extern const char* RESET;
extern const char* BOLD;
extern const char* RED;
extern const char* GREEN;
extern const char* YELLOW;
extern const char* BLUE;
extern const char* CYAN;
extern const char* DIM;
}  // namespace ansi

}  // namespace rxy::util
