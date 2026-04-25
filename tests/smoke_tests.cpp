// Basic test-runner smoke coverage.
//
// This file exists to prove the lightweight in-repo test harness can register
// and execute a test before the more specific compiler suites rely on it.
#include "test_support.hpp"

#include <string>

TEST_CASE(smoke_test_runner_executes)
{
	REQUIRE_EQ(std::string("rexc"), std::string("rexc"));
}
