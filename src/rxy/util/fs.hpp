#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace rxy::util {

// Walks upward from `start` looking for "Rexy.toml". Returns the directory
// containing the manifest, or nullopt if none found before the filesystem root.
std::optional<std::filesystem::path>
find_manifest_root(const std::filesystem::path& start);

// Read entire file as a string. Throws std::runtime_error on IO failure.
std::string read_text_file(const std::filesystem::path&);

// Atomically write `content` to `path` (write to .tmp, then rename).
void atomic_write_text_file(const std::filesystem::path& path,
                             const std::string& content);

// Resolves the rexc binary using this order:
//   1. $REXC env var
//   2. <dirname(/proc/self/exe)>/rexc — sibling lookup, mirrors cargo→rustc
//   3. PATH
// Returns absolute path; throws std::runtime_error with all 3 searched
// locations in the message if none found.
std::filesystem::path find_rexc(const std::filesystem::path& self_exe);

// Returns the path of the currently-running executable. macOS + Linux only.
std::filesystem::path current_executable_path();

}  // namespace rxy::util
