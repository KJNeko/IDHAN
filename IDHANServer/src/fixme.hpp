//
// Created by kj16609 on 7/23/24.
//

#pragma once

#include <iostream>
#include <source_location>

#include "logging/format_ns.hpp"

namespace idhan
{
inline void fixme( const std::source_location location = std::source_location::current() )
{
	std::cout << format_ns::format( "FIXME: {}:{} {}", location.file_name(), location.line(), location.function_name() )
			  << std::endl;
}

} // namespace idhan
