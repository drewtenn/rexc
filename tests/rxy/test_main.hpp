#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rxy::testing {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

std::vector<TestCase>& cases();

struct TestRegister {
    static bool add(const char* name, std::function<void()> fn);
};

}  // namespace rxy::testing

#define RXY_TEST_CONCAT2(a, b) a##b
#define RXY_TEST_CONCAT(a, b) RXY_TEST_CONCAT2(a, b)

#define RXY_TEST(name)                                                         \
    static void RXY_TEST_CONCAT(rxy_test_fn_, __LINE__)();                     \
    static const bool RXY_TEST_CONCAT(rxy_test_reg_, __LINE__) =               \
        ::rxy::testing::TestRegister::add(                                     \
            name, &RXY_TEST_CONCAT(rxy_test_fn_, __LINE__));                   \
    static void RXY_TEST_CONCAT(rxy_test_fn_, __LINE__)()

#define RXY_REQUIRE(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::ostringstream _oss;                                           \
            _oss << "REQUIRE(" #expr ") failed at " << __FILE__ << ":"         \
                 << __LINE__;                                                  \
            throw std::runtime_error(_oss.str());                              \
        }                                                                      \
    } while (0)

#define RXY_REQUIRE_EQ(a, b)                                                   \
    do {                                                                       \
        auto _ra = (a);                                                        \
        auto _rb = (b);                                                        \
        if (!(_ra == _rb)) {                                                   \
            std::ostringstream _oss;                                           \
            _oss << "REQUIRE_EQ(" #a ", " #b ") failed at " << __FILE__        \
                 << ":" << __LINE__ << " — lhs=" << _ra << " rhs=" << _rb;     \
            throw std::runtime_error(_oss.str());                              \
        }                                                                      \
    } while (0)
