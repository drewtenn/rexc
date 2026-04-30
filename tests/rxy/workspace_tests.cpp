#include "test_main.hpp"

#include "manifest/manifest.hpp"
#include "util/fs.hpp"
#include "workspace/workspace.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path make_tmp_dir(const std::string& prefix) {
    fs::path dir = fs::temp_directory_path() / (prefix + std::to_string(::rand()));
    fs::create_directories(dir);
    return dir;
}

void write_manifest(const fs::path& dir, const std::string& body) {
    fs::create_directories(dir);
    rxy::util::atomic_write_text_file(dir / "Rexy.toml", body);
}

void write_member_manifest(const fs::path& dir, const std::string& body) {
    write_manifest(dir, body);
    fs::create_directories(dir / "src");
    std::ofstream(dir / "src/main.rx") << "fn main() -> i32 { return 0; }\n";
}

rxy::manifest::Manifest load_member(const fs::path& manifest_path) {
    auto r = rxy::manifest::load_manifest(manifest_path);
    auto* m = std::get_if<rxy::manifest::Manifest>(&r);
    RXY_REQUIRE(m != nullptr);
    return *m;
}

bool has_path(const std::vector<fs::path>& paths, const fs::path& path) {
    fs::path canonical = fs::canonical(path);
    return std::find(paths.begin(), paths.end(), canonical) != paths.end();
}

}  // namespace

RXY_TEST("workspace: find_workspace_root walks upward") {
    fs::path root = make_tmp_dir("rxy_ws_walk_");
    fs::create_directories(root / "crates/app/src/deep");
    write_manifest(root, R"(
[workspace]
members = ["crates/app"]
)");
    write_member_manifest(root / "crates/app", R"(
[package]
name = "app"
version = "0.1.0"
edition = "2026"
)");

    auto found = rxy::workspace::find_workspace_root(root / "crates/app/src/deep");
    RXY_REQUIRE(found.has_value());
    RXY_REQUIRE(*found == fs::absolute(root));
}

RXY_TEST("workspace: find_workspace_root returns nullopt when none exists") {
    fs::path root = make_tmp_dir("rxy_ws_none_");
    fs::create_directories(root / "nested/project");
    write_manifest(root, R"(
[package]
name = "not_a_workspace"
version = "0.1.0"
edition = "2026"
)");

    auto found = rxy::workspace::find_workspace_root(root / "nested/project");
    RXY_REQUIRE(!found.has_value());
}

RXY_TEST("workspace: load parses members package defaults and dependencies") {
    fs::path root = make_tmp_dir("rxy_ws_load_");
    write_member_manifest(root / "crates/a", R"(
[package]
name = "a"
version = "0.1.0"
edition = "2026"
)");
    write_member_manifest(root / "crates/b", R"(
[package]
name = "b"
version = "0.1.0"
edition = "2026"
)");
    write_member_manifest(root / "tools/cli", R"(
[package]
name = "cli"
version = "0.1.0"
edition = "2026"
)");
    write_manifest(root, R"(
[workspace]
members = ["crates/*", "tools/cli"]

[workspace.package]
version = "1.2.3"
edition = "2026"
license = "MIT"
repository = "https://example.com/rexy"
description = "workspace defaults"

[workspace.dependencies]
foo = "0.4.0"
bar = { path = "vendor/bar" }
baz = { git = "https://example.com/baz.git", tag = "v1.0.0", version = "1.0" }
)");

    auto ws = rxy::workspace::load(root / "Rexy.toml");
    RXY_REQUIRE(ws.root == fs::absolute(root));
    RXY_REQUIRE(ws.members.size() == 3);
    RXY_REQUIRE(has_path(ws.members, root / "crates/a"));
    RXY_REQUIRE(has_path(ws.members, root / "crates/b"));
    RXY_REQUIRE(has_path(ws.members, root / "tools/cli"));
    RXY_REQUIRE(ws.default_version && *ws.default_version == "1.2.3");
    RXY_REQUIRE(ws.default_edition && *ws.default_edition == "2026");
    RXY_REQUIRE(ws.default_license && *ws.default_license == "MIT");
    RXY_REQUIRE(ws.default_repository && *ws.default_repository == "https://example.com/rexy");
    RXY_REQUIRE(ws.default_description && *ws.default_description == "workspace defaults");
    RXY_REQUIRE(ws.dependencies.size() == 3);
    RXY_REQUIRE(ws.dependencies.at("foo").registry_version &&
                *ws.dependencies.at("foo").registry_version == "0.4.0");
    RXY_REQUIRE(ws.dependencies.at("bar").path &&
                *ws.dependencies.at("bar").path == fs::path("vendor/bar"));
    RXY_REQUIRE(ws.dependencies.at("baz").git_url &&
                *ws.dependencies.at("baz").git_url == "https://example.com/baz.git");
    RXY_REQUIRE(ws.dependencies.at("baz").git_tag &&
                *ws.dependencies.at("baz").git_tag == "v1.0.0");
    RXY_REQUIRE(ws.dependencies.at("baz").git_version &&
                *ws.dependencies.at("baz").git_version == "1.0");
}

RXY_TEST("workspace: apply_inheritance resolves package version") {
    fs::path root = make_tmp_dir("rxy_ws_pkg_inherit_");
    write_manifest(root, R"(
[workspace]
members = ["member"]

[workspace.package]
version = "2.3.4"
edition = "2026"
)");
    write_member_manifest(root / "member", R"(
[package]
name = "member"
version.workspace = true
edition = "2026"
)");

    auto ws = rxy::workspace::load(root / "Rexy.toml");
    auto member = load_member(root / "member/Rexy.toml");
    RXY_REQUIRE(member.package.version_inherited);

    auto diagnostics = rxy::workspace::apply_inheritance(member, ws);
    RXY_REQUIRE(diagnostics.empty());
    RXY_REQUIRE(member.package.version == "2.3.4");
}

RXY_TEST("workspace: apply_inheritance diagnoses missing package field") {
    fs::path root = make_tmp_dir("rxy_ws_missing_inherit_");
    write_manifest(root, R"(
[workspace]
members = ["member"]

[workspace.package]
edition = "2026"
)");
    write_member_manifest(root / "member", R"(
[package]
name = "member"
version.workspace = true
edition = "2026"
)");

    auto ws = rxy::workspace::load(root / "Rexy.toml");
    auto member = load_member(root / "member/Rexy.toml");
    auto diagnostics = rxy::workspace::apply_inheritance(member, ws);

    RXY_REQUIRE(diagnostics.size() == 1);
    RXY_REQUIRE(diagnostics.front().message.find("version.workspace = true") != std::string::npos);
    RXY_REQUIRE(diagnostics.front().message.find("[workspace.package] version") != std::string::npos);
}

RXY_TEST("workspace: apply_inheritance resolves workspace dependency") {
    fs::path root = make_tmp_dir("rxy_ws_dep_inherit_");
    write_manifest(root, R"(
[workspace]
members = ["member"]

[workspace.dependencies]
foo = { path = "vendor/foo" }
)");
    write_member_manifest(root / "member", R"(
[package]
name = "member"
version = "0.1.0"
edition = "2026"

[dependencies]
foo.workspace = true
)");

    auto ws = rxy::workspace::load(root / "Rexy.toml");
    auto member = load_member(root / "member/Rexy.toml");
    RXY_REQUIRE(member.dependencies.size() == 1);
    RXY_REQUIRE(member.dependencies.front().from_workspace);

    auto diagnostics = rxy::workspace::apply_inheritance(member, ws);
    RXY_REQUIRE(diagnostics.empty());
    RXY_REQUIRE(member.dependencies.front().path.has_value());
    RXY_REQUIRE(*member.dependencies.front().path ==
                (fs::absolute(root) / "vendor/foo").lexically_normal());
}

RXY_TEST("workspace: apply_inheritance shadow merges profiles") {
    fs::path root = make_tmp_dir("rxy_ws_profile_inherit_");
    write_manifest(root, R"(
[workspace]
members = ["member"]

[workspace.profile.release]
opt-level = 3
debug = false
lto = true
strip = true
)");
    write_member_manifest(root / "member", R"(
[package]
name = "member"
version = "0.1.0"
edition = "2026"

[profile.release]
debug = true
strip = false
)");

    auto ws = rxy::workspace::load(root / "Rexy.toml");
    auto member = load_member(root / "member/Rexy.toml");
    auto diagnostics = rxy::workspace::apply_inheritance(member, ws);

    RXY_REQUIRE(diagnostics.empty());
    auto release = member.profiles.at("release");
    RXY_REQUIRE(release.opt_level && *release.opt_level == 3);
    RXY_REQUIRE(release.debug && *release.debug == true);
    RXY_REQUIRE(release.lto && *release.lto == true);
    RXY_REQUIRE(release.strip && *release.strip == false);
}
