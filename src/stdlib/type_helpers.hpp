#pragma once

#include "rexc/types.hpp"

namespace rexc::stdlib {

inline PrimitiveType i32_type()
{
	return PrimitiveType{PrimitiveKind::SignedInteger, 32};
}

inline PrimitiveType str_type()
{
	return PrimitiveType{PrimitiveKind::Str};
}

} // namespace rexc::stdlib
