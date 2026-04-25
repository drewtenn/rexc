#pragma once

#include "rexc/source.hpp"

#include <string>
#include <vector>

namespace rexc {

enum class DiagnosticSeverity {
	Error,
};

struct Diagnostic {
	DiagnosticSeverity severity;
	SourceLocation location;
	std::string message;
};

class Diagnostics {
public:
	void error(SourceLocation location, std::string message);
	bool has_errors() const;
	const std::vector<Diagnostic> &items() const;
	std::string format() const;

private:
	std::vector<Diagnostic> items_;
};

} // namespace rexc
