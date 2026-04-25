#include "rexc/source.hpp"

#include <algorithm>
#include <utility>

namespace rexc {

SourceFile::SourceFile(std::string path, std::string text)
	: path_(std::move(path)), text_(std::move(text))
{
	line_starts_.push_back(0);
	for (std::size_t i = 0; i < text_.size(); ++i) {
		if (text_[i] == '\n')
			line_starts_.push_back(i + 1);
	}
}

const std::string &SourceFile::path() const
{
	return path_;
}

const std::string &SourceFile::text() const
{
	return text_;
}

SourceLocation SourceFile::location_at(std::size_t offset) const
{
	if (offset > text_.size())
		offset = text_.size();

	auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
	std::size_t line_index = static_cast<std::size_t>(it - line_starts_.begin() - 1);
	std::size_t line_start = line_starts_[line_index];

	return SourceLocation{path_, offset, line_index + 1, offset - line_start + 1};
}

} // namespace rexc
