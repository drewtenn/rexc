#include "cache/cache.hpp"

#include "hash/sha256.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <unistd.h>

namespace rxy::cache {

namespace fs = std::filesystem;

fs::path cache_root() {
    const char* env = std::getenv("REXY_HOME");
    fs::path root;
    if (env && *env) {
        root = fs::path(env);
    } else {
        const char* home = std::getenv("HOME");
        if (!home) throw std::runtime_error("HOME env var is unset; cannot locate ~/.rxy");
        root = fs::path(home) / ".rxy";
    }
    ensure_dir(root);
    return root;
}

fs::path git_db_dir() {
    fs::path d = cache_root() / "git" / "db";
    ensure_dir(d);
    return d;
}

fs::path git_db_path_for_url(const std::string& url) {
    std::string h = hash::sha256_hex(url);
    if (h.rfind("sha256:", 0) == 0) h = h.substr(7);
    return git_db_dir() / (h + ".git");
}

fs::path src_path(const std::string& name, const std::string& commit) {
    fs::path d = cache_root() / "src" / name / commit;
    ensure_dir(d);
    return d;
}

void ensure_dir(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec && !fs::exists(p)) {
        throw std::runtime_error("could not create cache dir " + p.string() +
                                  ": " + ec.message());
    }
}

// ---------------------------------------------------------------------------
// FileLock — flock(2)-based advisory locks for cache concurrency.
// ---------------------------------------------------------------------------

FileLock::FileLock(const fs::path& lock_file) : path_(lock_file) {
    ensure_dir(lock_file.parent_path());
    fd_ = ::open(lock_file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd_ < 0) {
        throw std::runtime_error("could not open lock file " + lock_file.string() +
                                  ": " + std::strerror(errno));
    }
    int rc;
    while ((rc = ::flock(fd_, LOCK_EX)) < 0 && errno == EINTR) {}
    if (rc < 0) {
        int saved = errno;
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("flock(LOCK_EX) failed on " + lock_file.string() +
                                  ": " + std::strerror(saved));
    }
}

FileLock::FileLock(FileLock&& other) noexcept
    : fd_(other.fd_), path_(std::move(other.path_)) {
    other.fd_ = -1;
}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); }
        fd_ = other.fd_;
        path_ = std::move(other.path_);
        other.fd_ = -1;
    }
    return *this;
}

FileLock::~FileLock() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}

namespace {

fs::path bare_lock_path(const std::string& url) {
    std::string h = hash::sha256_hex(url);
    if (h.rfind("sha256:", 0) == 0) h = h.substr(7);
    return git_db_dir() / (h + ".lock");
}

fs::path src_lock_path(const std::string& name, const std::string& commit) {
    return cache_root() / "src" / name / (commit + ".lock");
}

}  // namespace

FileLock lock_bare_for_url(const std::string& url) {
    return FileLock(bare_lock_path(url));
}

FileLock lock_src(const std::string& name, const std::string& commit) {
    fs::path p = src_lock_path(name, commit);
    ensure_dir(p.parent_path());
    return FileLock(p);
}

}  // namespace rxy::cache
