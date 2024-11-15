//
// Created by kj16609 on 11/14/24.
//
#pragma once

#include <string>

namespace idhan
{

constexpr char TAG_NAMESPACE_DELIMTER { ':' };

std::pair< std::string, std::string > splitTag( const std::string_view str );

inline std::pair< std::string, std::string > splitTag( const std::string& str )
{
	const std::string_view view { str };
	return splitTag( view );
}

namespace tags
{

inline std::pair< std::string, std::string > split( const std::string_view str )
{
	return splitTag( str );
}

inline std::pair< std::string, std::string > split( const std::string& str )
{
	const std::string_view view { str };
	return splitTag( view );
}

} // namespace tags

} // namespace idhan
