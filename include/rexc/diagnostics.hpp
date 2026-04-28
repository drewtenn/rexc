#pragma once

// Shared diagnostic collection and formatting for compiler stages.
#include "rexc/source.hpp"

#include <string>
#include <vector>

namespace rexc {

enum class DiagnosticSeverity {
	Error,
};

struct SourceSpan {
	SourceLocation start;
	SourceLocation end;

	static SourceSpan from_location(SourceLocation location, std::size_t length = 0);
};

struct FixIt {
	std::string message;
	SourceSpan span;
	std::string replacement;
};

struct Diagnostic {
	DiagnosticSeverity severity;
	SourceLocation location;
	SourceSpan span;
	std::string message;
	std::vector<FixIt> fixits;
};

class Diagnostics {
public:
	void error(SourceLocation location, std::string message,
	           std::vector<FixIt> fixits = {});
	bool has_errors() const;
	const std::vector<Diagnostic> &items() const;
	std::string format() const;
	std::string format_json() const;

private:
	std::vector<Diagnostic> items_;
};

} // namespace rexc
