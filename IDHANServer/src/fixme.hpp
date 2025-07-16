//
// Created by kj16609 on 7/23/24.
//

#pragma once

#include <iostream>
#include <source_location>

namespace idhan
{
inline void fixme( const std::source_location location = std::source_location::current() )
{
	std::cout << format_ns::format( "{}:{} {}", location.file_name(), location.line(), location.function_name() )
			  << std::endl;
	std::abort();
}

} // namespace idhan