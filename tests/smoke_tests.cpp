// Basic test-runner smoke coverage.
#include "test_support.hpp"

#include <string>

TEST_CASE(smoke_test_runner_executes)
{
	REQUIRE_EQ(std::string("rexc"), std::string("rexc"));
}
