#pragma once

#include <cerrno>
#include <format>
#include <string>
#include <system_error>

namespace idhan
{

[[nodiscard]] inline std::string errnoMessage( const char* const what )
{
	return std::format( "{}: {}", what, std::generic_category().message( errno ) );
}

} // namespace idhan
