#include "test_main.hpp"

#include <cstdio>

namespace rxy::testing {

std::vector<TestCase>& cases() {
    static std::vector<TestCase> v;
    return v;
}

bool TestRegister::add(const char* name, std::function<void()> fn) {
    cases().push_back({name, std::move(fn)});
    return true;
}

}  // namespace rxy::testing

int main() {
    auto& v = rxy::testing::cases();
    int failed = 0;
    for (const auto& tc : v) {
        try {
            tc.fn();
            std::fprintf(stderr, "  ok   %s\n", tc.name);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "  FAIL %s: %s\n", tc.name, ex.what());
            ++failed;
        } catch (...) {
            std::fprintf(stderr, "  FAIL %s: unknown exception\n", tc.name);
            ++failed;
        }
    }
    std::fprintf(stderr, "\n%zu test(s), %d failure(s)\n", v.size(), failed);
    return failed == 0 ? 0 : 1;
}
