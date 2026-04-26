#pragma once

// Source text plus offset-to-line/column mapping for diagnostics.
#include <cstddef>
#include <string>
#include <vector>

namespace rexc {

struct SourceLocation {
	std::string file;
	std::size_t offset = 0;
	std::size_t line = 1;
	std::size_t column = 1;
};

class SourceFile {
public:
	SourceFile(std::string path, std::string text);

	const std::string &text() const;
	SourceLocation location_at(std::size_t offset) const;

private:
	std::string path_;
	std::string text_;
	std::vector<std::size_t> line_starts_;
};

} // namespace rexc
