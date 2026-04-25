#include "rexc/diagnostics.hpp"

#include <sstream>
#include <utility>

namespace rexc {

void Diagnostics::error(SourceLocation location, std::string message)
{
	items_.push_back({DiagnosticSeverity::Error, std::move(location), std::move(message)});
}

bool Diagnostics::has_errors() const
{
	return !items_.empty();
}

const std::vector<Diagnostic> &Diagnostics::items() const
{
	return items_;
}

std::string Diagnostics::format() const
{
	std::ostringstream out;

	for (const auto &item : items_) {
		out << item.location.file << ':' << item.location.line << ':'
		    << item.location.column << ": error: " << item.message << '\n';
	}

	return out.str();
}

} // namespace rexc
