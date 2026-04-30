#pragma once

#include <filesystem>
#include <string>

namespace rxy::cache {

// $REXY_HOME or ~/.rxy. Created on first call.
std::filesystem::path cache_root();

// $REXY_HOME/git/db — shared bare clones, keyed by sha256 of the URL.
std::filesystem::path git_db_dir();

// $REXY_HOME/git/db/<sha256-of-url>.git
std::filesystem::path git_db_path_for_url(const std::string& url);

// $REXY_HOME/src/<name>/<commit-sha>/  — checked-out worktree
std::filesystem::path src_path(const std::string& name, const std::string& commit);

// Best-effort mkdir -p; throws on failure.
void ensure_dir(const std::filesystem::path&);

// RAII advisory file lock (flock LOCK_EX). Two concurrent rxy processes that
// both try to clone/fetch the same git URL — or extract the same commit into
// the same worktree — would otherwise corrupt each other's cache; the locks
// serialize per-bucket so each bucket's clone-or-fetch and extract runs
// exclusively. Released on close (destructor) or process death.
class FileLock {
public:
    FileLock() = default;
    explicit FileLock(const std::filesystem::path& lock_file);
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    ~FileLock();

    bool held() const { return fd_ >= 0; }

private:
    int fd_ = -1;
    std::filesystem::path path_;
};

// Acquire an exclusive advisory lock on `<git_db>/<sha256-of-url>.lock`.
// Blocks until the lock is acquired. Lock file is created if absent.
FileLock lock_bare_for_url(const std::string& url);

// Acquire an exclusive advisory lock on `<src>/<name>/<commit>.lock`.
FileLock lock_src(const std::string& name, const std::string& commit);

}  // namespace rxy::cache
