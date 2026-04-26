#include "library.hpp"

namespace rexc::stdlib::core {

const std::vector<FunctionDecl> &prelude_functions()
{
	static const std::vector<FunctionDecl> functions;
	return functions;
}

} // namespace rexc::stdlib::core
