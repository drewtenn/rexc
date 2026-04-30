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

}  // namespace rxy::cache
