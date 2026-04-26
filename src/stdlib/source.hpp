#pragma once

#include "rexc/stdlib.hpp"

#include <string>
#include <vector>

namespace rexc::stdlib {

struct SourceUnit {
	Layer layer;
	std::string path;
	std::string source;
};

std::string portable_stdlib_source();
const std::vector<SourceUnit> &portable_stdlib_source_units();

} // namespace rexc::stdlib
