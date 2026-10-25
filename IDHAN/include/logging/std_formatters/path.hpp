#pragma once

#include <filesystem>
#include <format>
#include <string_view>

#ifndef __cpp_lib_format_path
// Older standard libraries format paths as strings; C++26 path-specific options
// are available only when the library supplies its own formatter.
template <>
struct std::formatter< std::filesystem::path > : std::formatter< std::string_view >
{
	template < typename FormatContext >
	auto format( const std::filesystem::path& path, FormatContext& context ) const
	{
		const auto text { path.string() };
		return std::formatter< std::string_view >::format( text, context );
	}
};
#endif
