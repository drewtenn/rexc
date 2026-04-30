#include "process/process.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <optional>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

#if RXY_HAVE_POSIX_SPAWN_ADDCHDIR_NP
extern "C" int posix_spawn_file_actions_addchdir_np(
    posix_spawn_file_actions_t* file_actions, const char* path);
#endif

namespace rxy::process {

namespace fs = std::filesystem;

namespace {

// Active-child registry: a small fixed-size table of pids, one per concurrent
// run() call. The signal handler iterates the table and forwards to every live
// child. This replaces the original single-pid global atomic so that concurrent
// run() calls (Phase E parallel build dispatch) don't lose track of children.
//
// pid_t is `int` on every platform rxy targets, and std::atomic<int> is always
// lock-free, which means atomic loads/stores are async-signal-safe — the only
// memory operations the handler actually performs.
constexpr size_t kMaxConcurrentChildren = 32;
static_assert(std::atomic<pid_t>::is_always_lock_free,
    "rxy requires lock-free std::atomic<pid_t> for async-signal-safe "
    "child-pid bookkeeping");

std::array<std::atomic<pid_t>, kMaxConcurrentChildren> g_active_children;

void init_active_table_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        for (auto& slot : g_active_children) slot.store(-1, std::memory_order_relaxed);
    });
}

class ChildSlot {
public:
    ChildSlot() : index_(static_cast<size_t>(-1)) {
        init_active_table_once();
        for (size_t i = 0; i < g_active_children.size(); ++i) {
            pid_t expected = -1;
            if (g_active_children[i].compare_exchange_strong(expected, 0,
                    std::memory_order_acq_rel)) {
                index_ = i;
                return;
            }
        }
        throw std::runtime_error(
            "rxy: too many concurrent child processes (limit " +
            std::to_string(kMaxConcurrentChildren) + ")");
    }
    void set(pid_t pid) noexcept {
        g_active_children[index_].store(pid, std::memory_order_release);
    }
    ~ChildSlot() {
        if (index_ != static_cast<size_t>(-1)) {
            g_active_children[index_].store(-1, std::memory_order_release);
        }
    }
    ChildSlot(const ChildSlot&) = delete;
    ChildSlot& operator=(const ChildSlot&) = delete;
private:
    size_t index_;
};

// Forwards the signal to every active child. If no child is active, restore
// the default disposition and re-raise so the parent process exits as the user
// expects (the typical case: user presses Ctrl-C during dependency resolution
// before any subprocess has been spawned).
void forward_signal(int sig) {
    bool any_forwarded = false;
    for (auto& slot : g_active_children) {
        pid_t pid = slot.load(std::memory_order_acquire);
        if (pid > 0) {
            ::kill(pid, sig);
            any_forwarded = true;
        }
    }
    if (!any_forwarded) {
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        ::sigaction(sig, &dfl, nullptr);
        ::raise(sig);
    }
}

// Install signal forwarders exactly once for the lifetime of the process. The
// previous (parent's) handlers are NOT restored; the forwarder itself reverts
// to SIG_DFL + raise() when no children are active, so the parent's effective
// disposition is preserved without per-call install/uninstall churn.
//
// Per-call install/uninstall would race with concurrent run() invocations:
// thread A's "uninstall" could remove the handler while thread B's child is
// still alive. install_once_forwarders() side-steps this entirely.
void install_once_forwarders() {
    static std::once_flag once;
    std::call_once(once, [] {
        struct sigaction sa{};
        sa.sa_handler = forward_signal;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        ::sigaction(SIGINT,  &sa, nullptr);
        ::sigaction(SIGTERM, &sa, nullptr);
    });
}

// Build a NULL-terminated argv array for posix_spawn.
struct ArgvHolder {
    std::vector<std::string> storage;
    std::vector<char*> ptrs;

    explicit ArgvHolder(const fs::path& exe, const std::vector<std::string>& args) {
        storage.reserve(args.size() + 1);
        storage.push_back(exe.string());
        for (const auto& a : args) storage.push_back(a);
        ptrs.reserve(storage.size() + 1);
        for (auto& s : storage) ptrs.push_back(s.data());
        ptrs.push_back(nullptr);
    }
};

// Compose envp from current environ + overlay.
struct EnvpHolder {
    std::vector<std::string> storage;
    std::vector<char*> ptrs;

    explicit EnvpHolder(const std::map<std::string, std::string>& overlay) {
        std::map<std::string, std::string> merged;
        for (char** e = environ; *e; ++e) {
            std::string entry = *e;
            auto eq = entry.find('=');
            if (eq == std::string::npos) continue;
            merged[entry.substr(0, eq)] = entry.substr(eq + 1);
        }
        for (const auto& kv : overlay) merged[kv.first] = kv.second;
        storage.reserve(merged.size());
        for (const auto& kv : merged) storage.push_back(kv.first + "=" + kv.second);
        ptrs.reserve(storage.size() + 1);
        for (auto& s : storage) ptrs.push_back(s.data());
        ptrs.push_back(nullptr);
    }
};

#if !RXY_HAVE_POSIX_SPAWN_ADDCHDIR_NP
// Fallback path: process-wide mutex serializes the parent-side chdir window so
// concurrent run() calls don't see each other's interleaved cwds. This is
// strictly worse than the in-child chdir (it serializes spawn) but is correct.
std::mutex& parent_chdir_mutex() {
    static std::mutex m;
    return m;
}
#endif

}  // namespace

Result run(const fs::path& exe,
           const std::vector<std::string>& args,
           const Options& opts) {
    Result r;

    install_once_forwarders();

    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions) != 0) {
        throw std::runtime_error("posix_spawn_file_actions_init failed");
    }

    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};

    if (!opts.stream_through) {
        if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
            ::posix_spawn_file_actions_destroy(&actions);
            throw std::runtime_error("pipe() failed");
        }
        ::posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
        ::posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
        ::posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
        ::posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
        ::posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
        ::posix_spawn_file_actions_addclose(&actions, err_pipe[1]);
    }

#if RXY_HAVE_POSIX_SPAWN_ADDCHDIR_NP
    if (!opts.cwd.empty()) {
        int rc = ::posix_spawn_file_actions_addchdir_np(&actions, opts.cwd.c_str());
        if (rc != 0) {
            ::posix_spawn_file_actions_destroy(&actions);
            if (out_pipe[0] != -1) { ::close(out_pipe[0]); ::close(out_pipe[1]); }
            if (err_pipe[0] != -1) { ::close(err_pipe[0]); ::close(err_pipe[1]); }
            throw std::runtime_error(
                std::string("posix_spawn_file_actions_addchdir_np failed: ") +
                std::strerror(rc));
        }
    }
#endif

    ArgvHolder argv(exe, args);
    EnvpHolder envp(opts.env_overlay);

    pid_t pid = -1;
    int spawn_err;

    // Reserve a slot before spawning so that a signal arriving between spawn
    // and slot.set(pid) cannot find an empty table — the slot is held with
    // pid==0 (a guard value) until the real pid is known.
    ChildSlot slot;

    {
#if !RXY_HAVE_POSIX_SPAWN_ADDCHDIR_NP
        // Fallback: chdir in the parent. Hold the mutex across the entire
        // chdir+spawn+restore window so concurrent callers don't observe each
        // other's transient cwd.
        std::optional<std::lock_guard<std::mutex>> chdir_lock;
        fs::path saved_cwd;
        bool cwd_changed = false;
        if (!opts.cwd.empty()) {
            chdir_lock.emplace(parent_chdir_mutex());
            std::error_code ec;
            saved_cwd = fs::current_path(ec);
            if (ec) saved_cwd.clear();
            std::error_code ec2;
            fs::current_path(opts.cwd, ec2);
            cwd_changed = !ec2;
            if (!cwd_changed) {
                ::posix_spawn_file_actions_destroy(&actions);
                if (out_pipe[0] != -1) { ::close(out_pipe[0]); ::close(out_pipe[1]); }
                if (err_pipe[0] != -1) { ::close(err_pipe[0]); ::close(err_pipe[1]); }
                throw std::runtime_error("could not chdir to " + opts.cwd.string() +
                    ": " + ec2.message());
            }
        }
        spawn_err = ::posix_spawn(&pid, exe.c_str(),
                                   &actions, nullptr,
                                   argv.ptrs.data(), envp.ptrs.data());
        if (cwd_changed && !saved_cwd.empty()) {
            std::error_code ec;
            fs::current_path(saved_cwd, ec);
        }
#else
        spawn_err = ::posix_spawn(&pid, exe.c_str(),
                                   &actions, nullptr,
                                   argv.ptrs.data(), envp.ptrs.data());
#endif
    }

    ::posix_spawn_file_actions_destroy(&actions);

    if (spawn_err != 0) {
        if (out_pipe[0] != -1) { ::close(out_pipe[0]); ::close(out_pipe[1]); }
        if (err_pipe[0] != -1) { ::close(err_pipe[0]); ::close(err_pipe[1]); }
        throw std::runtime_error(std::string("posix_spawn failed: ") + std::strerror(spawn_err));
    }

    slot.set(pid);

    if (!opts.stream_through) {
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);

        auto drain = [](int fd, std::string& out) {
            char buf[4096];
            while (true) {
                ssize_t n = ::read(fd, buf, sizeof(buf));
                if (n > 0) out.append(buf, static_cast<size_t>(n));
                else if (n == 0) break;
                else if (errno == EINTR) continue;
                else break;
            }
        };
        drain(out_pipe[0], r.stdout_data);
        drain(err_pipe[0], r.stderr_data);
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
    }

    int status = 0;
    pid_t wp = -1;
    while ((wp = ::waitpid(pid, &status, 0)) < 0 && errno == EINTR) {}

    if (wp < 0) {
        throw std::runtime_error(std::string("waitpid failed: ") + std::strerror(errno));
    }

    if (WIFEXITED(status))         r.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))  r.exit_code = 128 + WTERMSIG(status);
    else                            r.exit_code = -1;

    return r;
}

}  // namespace rxy::process
