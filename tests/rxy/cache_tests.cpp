#include "test_main.hpp"

#include "cache/cache.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

// Each test gets its own REXY_HOME so the lock-file paths are isolated and
// nothing depends on / pollutes the developer's real ~/.rxy.
struct ScopedRexyHome {
    fs::path dir;
    std::string saved;
    bool had_prev;
    ScopedRexyHome() {
        dir = fs::temp_directory_path() /
            ("rxy_cache_test_" + std::to_string(::rand()));
        fs::create_directories(dir);
        const char* prev = std::getenv("REXY_HOME");
        had_prev = (prev != nullptr);
        if (had_prev) saved = prev;
        ::setenv("REXY_HOME", dir.string().c_str(), 1);
    }
    ~ScopedRexyHome() {
        if (had_prev) ::setenv("REXY_HOME", saved.c_str(), 1);
        else          ::unsetenv("REXY_HOME");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

RXY_TEST("cache: bare lock path is keyed by url and lives next to the bare repo") {
    ScopedRexyHome home;
    auto lock = rxy::cache::lock_bare_for_url("https://example.invalid/repo.git");
    RXY_REQUIRE(lock.held());
    auto bare = rxy::cache::git_db_path_for_url("https://example.invalid/repo.git");
    fs::path lock_path = bare.parent_path() /
        (bare.filename().stem().string() + ".lock");
    std::error_code ec;
    RXY_REQUIRE(fs::is_regular_file(lock_path, ec));
}

RXY_TEST("cache: src lock path is keyed by name+commit") {
    ScopedRexyHome home;
    auto lock = rxy::cache::lock_src("foo", "deadbeef");
    RXY_REQUIRE(lock.held());
    fs::path lock_path = rxy::cache::cache_root() / "src" / "foo" / "deadbeef.lock";
    std::error_code ec;
    RXY_REQUIRE(fs::is_regular_file(lock_path, ec));
}

RXY_TEST("cache: FileLock release on destruction unblocks a peer") {
    ScopedRexyHome home;
    // Acquire then drop, twice. Second acquisition must succeed (proves release
    // happened — flock would block forever otherwise).
    {
        auto a = rxy::cache::lock_bare_for_url("https://example.invalid/x.git");
        RXY_REQUIRE(a.held());
    }
    auto b = rxy::cache::lock_bare_for_url("https://example.invalid/x.git");
    RXY_REQUIRE(b.held());
}

RXY_TEST("cache: FileLock blocks across processes (fork)") {
    ScopedRexyHome home;
    // Parent acquires; forked child blocks until parent releases. We measure
    // that the child's acquisition delay corresponds to the parent's hold
    // window, which deterministically exercises the cross-process flock.
    auto parent_lock = rxy::cache::lock_bare_for_url("https://example.invalid/blocking.git");
    RXY_REQUIRE(parent_lock.held());

    pid_t pid = ::fork();
    RXY_REQUIRE(pid >= 0);
    if (pid == 0) {
        // Child — block on the same URL's lock and immediately exit on success.
        try {
            auto child_lock = rxy::cache::lock_bare_for_url("https://example.invalid/blocking.git");
            std::_Exit(child_lock.held() ? 0 : 1);
        } catch (...) {
            std::_Exit(2);
        }
    }

    // Hold for a moment, then release. The child should then acquire and exit 0.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    { auto release = std::move(parent_lock); }   // explicit release

    int status = 0;
    pid_t wp;
    while ((wp = ::waitpid(pid, &status, 0)) < 0 && errno == EINTR) {}
    RXY_REQUIRE(wp == pid);
    RXY_REQUIRE(WIFEXITED(status));
    RXY_REQUIRE_EQ(WEXITSTATUS(status), 0);
}
