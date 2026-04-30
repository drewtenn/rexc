#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace rxy::hash {

// SHA-256, used by rxy for content-addressable identity of dependency
// source trees and for lockfile checksum verification.

struct Sha256State {
    uint8_t  data[64]{};
    uint32_t datalen = 0;
    uint64_t bitlen  = 0;
    uint32_t state[8]{};
};

void sha256_init(Sha256State&);
void sha256_update(Sha256State&, const uint8_t* data, size_t len);
void sha256_final(Sha256State&, std::array<uint8_t, 32>& out_hash);

// Convenience: hex-encoded "sha256:<hex>" string.
std::string sha256_hex(const std::string& s);
std::string sha256_hex(const uint8_t* data, size_t len);

// Walk a directory deterministically (lexical order), skipping
// `target/`, `.git/`, and `Rexy.lock`. For each file, fold the
// relative path and contents into the hash. Returns "sha256:<hex>".
std::string sha256_dir_tree(const std::filesystem::path& root);

}  // namespace rxy::hash
