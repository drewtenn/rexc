#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rxy::process {

struct Result {
    int exit_code = 0;
    std::string stdout_data;     // empty if streamed through
    std::string stderr_data;     // empty if streamed through
};

struct Options {
    std::filesystem::path cwd;
    std::map<std::string, std::string> env_overlay;     // applied on top of inherited env
    bool stream_through = true;                          // pass child stdio to parent stdio
    bool inherit_color  = true;                          // detected by caller; no-op in this struct
};

// Run a child process via posix_spawn. Forwards SIGINT/SIGTERM to the child
// while it runs, restoring the parent's signal handlers afterwards.
Result run(const std::filesystem::path& exe,
           const std::vector<std::string>& args,
           const Options& opts);

}  // namespace rxy::process
