//
// Created by kj16609 on 11/14/24.
//

#include "splitTag.hpp"

namespace idhan
{

std::pair< std::string, std::string > splitTag( const std::string_view str )
{
	std::string namespace_str {};
	std::string subtag_str {};

	auto split_itter { str.find_first_of( TAG_NAMESPACE_DELIMTER ) };

	if ( split_itter == std::string::npos )
	{
		subtag_str = str;
		return std::make_pair( std::move( namespace_str ), std::move( subtag_str ) );
	}

	namespace_str = str.substr( 0, split_itter );
	subtag_str = str.substr( split_itter + 1 );

	return std::make_pair( std::move( namespace_str ), std::move( subtag_str ) );
}

} // namespace idhan