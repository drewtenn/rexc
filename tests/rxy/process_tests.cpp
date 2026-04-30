#include "test_main.hpp"

#include "process/process.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

// Phase E predecessor: posix_spawn_file_actions_addchdir_np (or the parent
// chdir fallback) must materialize the requested cwd in the child without
// permanently changing the parent's cwd.
RXY_TEST("process: cwd option lands in the child without leaking to the parent") {
    fs::path tmpdir = fs::temp_directory_path() /
        ("rxy_proc_cwd_" + std::to_string(::rand()));
    fs::create_directories(tmpdir);

    fs::path parent_cwd_before = fs::current_path();

    rxy::process::Options opts;
    opts.cwd = tmpdir;
    opts.stream_through = false;
    auto r = rxy::process::run("/bin/pwd", {}, opts);
    RXY_REQUIRE_EQ(r.exit_code, 0);

    fs::path parent_cwd_after = fs::current_path();
    RXY_REQUIRE(parent_cwd_before == parent_cwd_after);

    // /bin/pwd output should resolve to tmpdir (after canonicalization, since
    // /tmp on macOS is a symlink to /private/tmp).
    std::string out = r.stdout_data;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    fs::path got = fs::canonical(fs::path(out));
    fs::path want = fs::canonical(tmpdir);
    RXY_REQUIRE_EQ(got, want);

    std::error_code ec;
    fs::remove_all(tmpdir, ec);
}

// Sanity: many sequential spawns must not exhaust the active-child slot table.
// Each run() call claims a slot during the spawn and releases it on return; a
// missing release would surface as "too many concurrent child processes" before
// hitting the 32-slot ceiling.
RXY_TEST("process: sequential spawns do not leak slot-table entries") {
    rxy::process::Options opts;
    opts.stream_through = false;
    for (int i = 0; i < 64; ++i) {
        auto r = rxy::process::run("/usr/bin/true", {}, opts);
        RXY_REQUIRE_EQ(r.exit_code, 0);
    }
}

// Failed spawns (bad executable path) must not leak a slot either — the slot
// guard releases regardless of whether posix_spawn succeeded.
RXY_TEST("process: failed spawns release their slot") {
    rxy::process::Options opts;
    opts.stream_through = false;
    int fails = 0;
    for (int i = 0; i < 64; ++i) {
        try {
            (void)rxy::process::run("/nonexistent/rxy_should_fail", {}, opts);
        } catch (const std::exception&) {
            ++fails;
        }
    }
    RXY_REQUIRE_EQ(fails, 64);
    // Now a successful run must still work — proves we did not exhaust slots.
    auto r = rxy::process::run("/usr/bin/true", {}, opts);
    RXY_REQUIRE_EQ(r.exit_code, 0);
}
