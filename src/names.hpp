#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace rexc {

inline std::string join_path(const std::vector<std::string> &segments,
                             const std::string &separator)
{
	std::ostringstream joined;
	for (std::size_t i = 0; i < segments.size(); ++i) {
		if (i > 0)
			joined << separator;
		joined << segments[i];
	}
	return joined.str();
}

inline std::vector<std::string> item_path(std::vector<std::string> module_path,
                                          const std::string &name)
{
	module_path.push_back(name);
	return module_path;
}

inline std::string canonical_path(const std::vector<std::string> &segments)
{
	return join_path(segments, "::");
}

inline std::string symbol_path(const std::vector<std::string> &segments)
{
	return join_path(segments, "_");
}

inline std::string canonical_item_path(const std::vector<std::string> &module_path,
                                       const std::string &name)
{
	return canonical_path(item_path(module_path, name));
}

inline std::string symbol_item_path(const std::vector<std::string> &module_path,
                                    const std::string &name)
{
	return symbol_path(item_path(module_path, name));
}

inline std::optional<std::vector<std::string>> stdlib_path_for_symbol(
    const std::string &symbol)
{
	const std::string prefix = "std_";
	if (symbol.rfind(prefix, 0) != 0)
		return std::nullopt;

	auto module_start = prefix.size();
	auto module_end = symbol.find('_', module_start);
	if (module_end == std::string::npos || module_end == module_start ||
	    module_end + 1 >= symbol.size())
		return std::nullopt;

	return std::vector<std::string>{
	    "std", symbol.substr(module_start, module_end - module_start),
	    symbol.substr(module_end + 1)};
}

} // namespace rexc
