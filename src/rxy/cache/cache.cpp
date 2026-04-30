#include "cache/cache.hpp"

#include "hash/sha256.hpp"

#include <cstdlib>
#include <stdexcept>

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

}  // namespace rxy::cache
