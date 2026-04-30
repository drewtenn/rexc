#include "process/process.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace rxy::process {

namespace fs = std::filesystem;

namespace {

std::atomic<pid_t> g_active_child{-1};

void forward_signal(int sig) {
    pid_t pid = g_active_child.load();
    if (pid > 0) ::kill(pid, sig);
}

struct ScopedSignalForwarders {
    struct sigaction prev_int{};
    struct sigaction prev_term{};
    bool installed = false;

    void install() {
        struct sigaction sa{};
        sa.sa_handler = forward_signal;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        ::sigaction(SIGINT,  &sa, &prev_int);
        ::sigaction(SIGTERM, &sa, &prev_term);
        installed = true;
    }
    ~ScopedSignalForwarders() {
        if (installed) {
            ::sigaction(SIGINT,  &prev_int,  nullptr);
            ::sigaction(SIGTERM, &prev_term, nullptr);
        }
    }
};

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

}  // namespace

Result run(const fs::path& exe,
           const std::vector<std::string>& args,
           const Options& opts) {
    Result r;

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

    if (!opts.cwd.empty()) {
        // posix_spawn_file_actions_addchdir_np is non-standard; macOS has it,
        // older Linux glibc may not. Fall back to chdir-on-fork via a small
        // helper: use spawn-then-no-chdir if not available. For Phase A, all
        // current call sites pass cwd matching the current working directory,
        // so we simply ::chdir() in the parent before spawn and restore.
    }

    ArgvHolder argv(exe, args);
    EnvpHolder envp(opts.env_overlay);

    ScopedSignalForwarders forwarders;
    forwarders.install();

    pid_t pid = -1;

    fs::path saved_cwd;
    bool cwd_changed = false;
    if (!opts.cwd.empty()) {
        std::error_code ec;
        saved_cwd = fs::current_path(ec);
        if (ec) saved_cwd.clear();
        std::error_code ec2;
        fs::current_path(opts.cwd, ec2);
        cwd_changed = !ec2;
    }

    int spawn_err = ::posix_spawn(&pid, exe.c_str(),
                                   &actions, nullptr,
                                   argv.ptrs.data(), envp.ptrs.data());

    if (cwd_changed && !saved_cwd.empty()) {
        std::error_code ec;
        fs::current_path(saved_cwd, ec);
    }

    ::posix_spawn_file_actions_destroy(&actions);

    if (spawn_err != 0) {
        if (out_pipe[0] != -1) { ::close(out_pipe[0]); ::close(out_pipe[1]); }
        if (err_pipe[0] != -1) { ::close(err_pipe[0]); ::close(err_pipe[1]); }
        throw std::runtime_error(std::string("posix_spawn failed: ") + std::strerror(spawn_err));
    }

    g_active_child.store(pid);

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
    g_active_child.store(-1);

    if (wp < 0) {
        throw std::runtime_error(std::string("waitpid failed: ") + std::strerror(errno));
    }

    if (WIFEXITED(status))         r.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))  r.exit_code = 128 + WTERMSIG(status);
    else                            r.exit_code = -1;

    return r;
}

}  // namespace rxy::process
