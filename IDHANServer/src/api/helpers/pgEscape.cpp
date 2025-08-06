//
// Created by kj16609 on 7/21/25.
//

#include <string>

#include "fgl/defines.hpp"
#include "logging/format_ns.hpp"

namespace idhan::api::helpers
{

constexpr std::string pgEscapeI( const std::string& str )
{
	std::string cleaned {};
	cleaned.reserve( str.size() * 2 );

	// if ( str.empty() ) return "\"\"";
	if ( str == "null" ) return "\"null\"";

	bool contains_comma { false };

	for ( const auto& c : str )
	{
		switch ( c )
		{
			case '\'':
				cleaned.push_back( '\'' );
				cleaned.push_back( '\'' );
				break;
			case '}':
				[[fallthrough]];
			case '{':
				[[fallthrough]];
			case '\"':
				[[fallthrough]];
			case '\\':
				cleaned.push_back( '\\' );
				[[fallthrough]];
			case ',':
				contains_comma = contains_comma || ( c == ',' );
				[[fallthrough]];
			default:
				cleaned.push_back( c );
		}
	}

	if ( contains_comma ) return format_ns::format( "\"{}\"", cleaned );
	return cleaned;
}

static_assert( pgEscapeI( "Hello" ) == "Hello" );
static_assert( pgEscapeI( "amos' bow" ) == "amos'' bow" );

FGL_FLATTEN std::string pgEscape( const std::string& str )
{
	return pgEscapeI( str );
}

} // namespace idhan::api::helpers