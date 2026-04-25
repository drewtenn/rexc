#include "test_support.hpp"

#include <exception>
#include <iostream>
#include <utility>
#include <vector>

std::vector<TestCase> &test_registry()
{
	static std::vector<TestCase> tests;
	return tests;
}

void register_test(const char *name, std::function<void()> run)
{
	test_registry().push_back({name, std::move(run)});
}

int main()
{
	int failed = 0;

	for (const auto &test : test_registry()) {
		try {
			test.run();
			std::cout << "PASS " << test.name << '\n';
		} catch (const std::exception &err) {
			++failed;
			std::cerr << "FAIL " << test.name << ": " << err.what() << '\n';
		}
	}

	return failed == 0 ? 0 : 1;
}
