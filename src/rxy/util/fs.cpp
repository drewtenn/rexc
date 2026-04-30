#include "util/fs.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <unistd.h>
#include <sys/stat.h>

namespace rxy::util {

namespace fs = std::filesystem;

std::optional<fs::path> find_manifest_root(const fs::path& start) {
    fs::path dir = fs::absolute(start);
    if (fs::is_regular_file(dir)) dir = dir.parent_path();
    while (true) {
        fs::path candidate = dir / "Rexy.toml";
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) return dir;
        if (dir == dir.root_path()) return std::nullopt;
        fs::path parent = dir.parent_path();
        if (parent == dir) return std::nullopt;
        dir = parent;
    }
}

std::string read_text_file(const fs::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        throw std::runtime_error("could not open file: " + path.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void atomic_write_text_file(const fs::path& path, const std::string& content) {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("could not open for write: " + tmp.string());
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) {
            throw std::runtime_error("write failed: " + tmp.string());
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        throw std::runtime_error("atomic rename failed: " + ec.message());
    }
}

fs::path current_executable_path() {
#if defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        throw std::runtime_error("_NSGetExecutablePath: buffer too small");
    }
    return fs::canonical(fs::path(buf));
#elif defined(__linux__)
    char buf[4096];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len < 0) {
        throw std::runtime_error("readlink /proc/self/exe failed");
    }
    buf[len] = '\0';
    return fs::path(buf);
#else
    throw std::runtime_error("current_executable_path: unsupported platform");
#endif
}

namespace {
bool is_executable(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    return ::access(p.c_str(), X_OK) == 0;
}

std::optional<fs::path> search_path(const std::string& exe_name) {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return std::nullopt;
    std::string path_str = path_env;
    std::string::size_type start = 0;
    while (start <= path_str.size()) {
        std::string::size_type end = path_str.find(':', start);
        std::string entry = (end == std::string::npos)
            ? path_str.substr(start)
            : path_str.substr(start, end - start);
        if (!entry.empty()) {
            fs::path candidate = fs::path(entry) / exe_name;
            if (is_executable(candidate)) return candidate;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return std::nullopt;
}
}  // namespace

fs::path find_on_path(const std::string& exe_name, const char* env_var) {
    if (env_var) {
        if (const char* p = std::getenv(env_var); p && *p) {
            fs::path candidate = p;
            if (is_executable(candidate)) return fs::canonical(candidate);
            throw std::runtime_error(std::string{env_var} +
                "=" + p + " does not point to an executable");
        }
    }
    auto on_path = search_path(exe_name);
    if (on_path) return fs::canonical(*on_path);
    throw std::runtime_error("could not find `" + exe_name + "` on PATH" +
        (env_var ? std::string("; set $") + env_var + " to override" : ""));
}

fs::path find_rexc(const fs::path& self_exe) {
    std::vector<std::string> tried;

    if (const char* env = std::getenv("REXC")) {
        fs::path p = env;
        tried.push_back("$REXC=" + p.string());
        if (is_executable(p)) return fs::canonical(p);
    } else {
        tried.push_back("$REXC (unset)");
    }

    fs::path sibling = self_exe.parent_path() / "rexc";
    tried.push_back("sibling: " + sibling.string());
    if (is_executable(sibling)) return fs::canonical(sibling);

    auto on_path = search_path("rexc");
    if (on_path) {
        tried.push_back("PATH: " + on_path->string());
        return fs::canonical(*on_path);
    } else {
        tried.push_back("PATH: not found");
    }

    std::ostringstream oss;
    oss << "could not locate `rexc` compiler. searched:";
    for (const auto& t : tried) oss << "\n  - " << t;
    oss << "\nhelp: set REXC env var, or place rxy beside rexc in the same dir, or add rexc to PATH";
    throw std::runtime_error(oss.str());
}

}  // namespace rxy::util
