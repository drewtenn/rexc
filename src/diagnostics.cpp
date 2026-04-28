// Shared diagnostic storage and formatting.
//
// Compiler stages append source-located errors here instead of printing
// directly. The CLI can then decide whether to continue to the next stage and
// render all collected messages in a stable file:line:column format.
#include "rexc/diagnostics.hpp"

#include <cstddef>
#include <sstream>
#include <utility>

namespace rexc {

namespace {

constexpr std::size_t kHumanDiagnosticLineCap = 8;

void write_json_string(std::ostringstream &out, const std::string &value)
{
	out << '"';
	for (char ch : value) {
		switch (ch) {
		case '\\':
			out << "\\\\";
			break;
		case '"':
			out << "\\\"";
			break;
		case '\b':
			out << "\\b";
			break;
		case '\f':
			out << "\\f";
			break;
		case '\n':
			out << "\\n";
			break;
		case '\r':
			out << "\\r";
			break;
		case '\t':
			out << "\\t";
			break;
		default:
			out << ch;
			break;
		}
	}
	out << '"';
}

void write_json_location(std::ostringstream &out, const SourceLocation &location)
{
	out << "{ \"offset\": " << location.offset << ", \"line\": " << location.line
	    << ", \"column\": " << location.column << " }";
}

void write_json_span(std::ostringstream &out, const SourceSpan &span,
                     const std::string &indent)
{
	out << "{\n";
	out << indent << "  \"file\": ";
	write_json_string(out, span.start.file);
	out << ",\n";
	out << indent << "  \"start\": ";
	write_json_location(out, span.start);
	out << ",\n";
	out << indent << "  \"end\": ";
	write_json_location(out, span.end);
	out << "\n";
	out << indent << "}";
}

} // namespace

SourceSpan SourceSpan::from_location(SourceLocation location, std::size_t length)
{
	SourceLocation end = location;
	end.offset += length;
	end.column += length;
	return SourceSpan{std::move(location), std::move(end)};
}

void Diagnostics::error(SourceLocation location, std::string message,
                        std::vector<FixIt> fixits)
{
	auto span = SourceSpan::from_location(location);
	if (!fixits.empty())
		span = fixits.front().span;
	items_.push_back({DiagnosticSeverity::Error, std::move(location), std::move(span),
	                  std::move(message), std::move(fixits)});
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

	std::size_t visible_items = items_.size();
	bool capped = visible_items > kHumanDiagnosticLineCap;
	if (capped)
		visible_items = kHumanDiagnosticLineCap - 1;

	for (std::size_t i = 0; i < visible_items; ++i) {
		const auto &item = items_[i];
		out << item.location.file << ':' << item.location.line << ':'
		    << item.location.column << ": error: " << item.message << '\n';
	}

	if (capped) {
		out << "error: too many diagnostics, omitted "
		    << (items_.size() - visible_items) << " more\n";
	}

	return out.str();
}

std::string Diagnostics::format_json() const
{
	std::ostringstream out;
	out << "[\n";
	for (std::size_t i = 0; i < items_.size(); ++i) {
		const auto &item = items_[i];
		out << "  {\n";
		out << "    \"severity\": \"error\",\n";
		out << "    \"message\": ";
		write_json_string(out, item.message);
		out << ",\n";
		out << "    \"span\": ";
		write_json_span(out, item.span, "    ");
		out << ",\n";
		out << "    \"fixits\": [";
		if (!item.fixits.empty())
			out << '\n';
		for (std::size_t fixit_index = 0; fixit_index < item.fixits.size();
		     ++fixit_index) {
			const auto &fixit = item.fixits[fixit_index];
			out << "      {\n";
			out << "        \"message\": ";
			write_json_string(out, fixit.message);
			out << ",\n";
			out << "        \"replacement\": ";
			write_json_string(out, fixit.replacement);
			out << ",\n";
			out << "        \"span\": ";
			write_json_span(out, fixit.span, "        ");
			out << "\n";
			out << "      }";
			if (fixit_index + 1 < item.fixits.size())
				out << ',';
			out << '\n';
		}
		if (!item.fixits.empty())
			out << "    ";
		out << "]\n";
		out << "  }";
		if (i + 1 < items_.size())
			out << ',';
		out << '\n';
	}
	out << "]\n";
	return out.str();
}

} // namespace rexc
