#include "test_main.hpp"

#include "util/fs.hpp"
#include "util/tty.hpp"

#include <filesystem>
#include <fstream>

RXY_TEST("util/fs: find_manifest_root walks upward") {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / ("rxy_walk_" + std::to_string(::rand()));
    fs::create_directories(root / "a/b/c");
    rxy::util::atomic_write_text_file(root / "Rexy.toml", "[package]\nname=\"x\"\n");
    auto r = rxy::util::find_manifest_root(root / "a/b/c");
    RXY_REQUIRE(r.has_value());
    RXY_REQUIRE(*r == root);
}

RXY_TEST("util/fs: find_manifest_root returns nullopt when none exists") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("rxy_none_" + std::to_string(::rand()));
    fs::create_directories(dir);
    auto r = rxy::util::find_manifest_root(dir);
    RXY_REQUIRE(!r.has_value());
}

RXY_TEST("util/tty: color mode toggles") {
    using rxy::util::ColorMode;
    rxy::util::set_color_mode(ColorMode::Always);
    RXY_REQUIRE(rxy::util::color_enabled_for_stderr());
    rxy::util::set_color_mode(ColorMode::Never);
    RXY_REQUIRE(!rxy::util::color_enabled_for_stderr());
    rxy::util::set_color_mode(ColorMode::Auto);
}
