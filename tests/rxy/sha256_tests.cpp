#include "test_main.hpp"

#include "hash/sha256.hpp"

#include <filesystem>
#include <fstream>
#include <string>

// SHA-256 known-answer-tests from the original FIPS 180-2 / RFC 6234 spec.
RXY_TEST("sha256: empty string KAT") {
    RXY_REQUIRE_EQ(rxy::hash::sha256_hex(std::string{}),
        std::string("sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

RXY_TEST("sha256: 'abc' KAT") {
    RXY_REQUIRE_EQ(rxy::hash::sha256_hex(std::string("abc")),
        std::string("sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

RXY_TEST("sha256: 56-byte boundary KAT") {
    std::string s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    RXY_REQUIRE_EQ(rxy::hash::sha256_hex(s),
        std::string("sha256:248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

RXY_TEST("sha256_dir_tree: deterministic + skips target/") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("rxy_dirhash_" + std::to_string(::rand()));
    fs::create_directories(dir / "src");
    fs::create_directories(dir / "target" / "dev");
    {
        std::ofstream(dir / "Rexy.toml") << "[package]\nname=\"x\"\nversion=\"0.1.0\"\nedition=\"2026\"\n";
        std::ofstream(dir / "src/main.rx") << "fn main() -> i32 { return 0; }\n";
        std::ofstream(dir / "target/dev/x") << "BINARY-NOISE\n";
    }
    auto h1 = rxy::hash::sha256_dir_tree(dir);

    // Mutating target/ should NOT change the hash.
    { std::ofstream(dir / "target/dev/x", std::ios::trunc) << "DIFFERENT-NOISE\n"; }
    auto h2 = rxy::hash::sha256_dir_tree(dir);
    RXY_REQUIRE_EQ(h1, h2);

    // Mutating src/ MUST change the hash.
    { std::ofstream(dir / "src/main.rx", std::ios::trunc) << "fn main() -> i32 { return 1; }\n"; }
    auto h3 = rxy::hash::sha256_dir_tree(dir);
    RXY_REQUIRE(h1 != h3);
}
