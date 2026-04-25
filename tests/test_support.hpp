#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

struct TestCase {
	const char *name;
	std::function<void()> run;
};

std::vector<TestCase> &test_registry();
void register_test(const char *name, std::function<void()> run);

#define TEST_CASE(name) \
	static void name(); \
	static const bool name##_registered = [] { \
		register_test(#name, name); \
		return true; \
	}(); \
	static void name()

#define REQUIRE(value) \
	do { \
		if (!(value)) \
			throw std::runtime_error("requirement failed: " #value); \
	} while (false)

#define REQUIRE_EQ(actual, expected) \
	do { \
		if (!((actual) == (expected))) \
			throw std::runtime_error(std::string("requirement failed: ") + #actual + " == " + #expected); \
	} while (false)
