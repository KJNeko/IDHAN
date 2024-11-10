//
// Created by kj16609 on 9/8/24.
//

#include "tags.hpp"

namespace idhan::tags
{

std::pair< std::string, std::string > split( std::string_view str )
{
	std::string namespace_str {};
	std::string subtag_str {};

	auto split_itter { str.find_first_of( NAMESPACE_DELIMTER ) };
	namespace_str = str.substr( 0, split_itter );
	subtag_str = str.substr( split_itter );

	return std::make_pair( std::move( namespace_str ), std::move( subtag_str ) );
}



} // namespace idhan::tags
