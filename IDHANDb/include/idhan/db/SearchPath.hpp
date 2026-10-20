#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace idhan::db
{

inline constexpr std::string_view DEFAULT_SCHEMA { "idhan" };
inline constexpr std::string_view EXTENSION_SCHEMA { "public" };

// A schema setting names one identifier, even when it contains commas or quotes.
[[nodiscard]] inline std::string makeSearchPath( const std::string_view schema )
{
	if ( schema.empty() || schema.find( '\0' ) != std::string_view::npos )
		throw std::invalid_argument( "PostgreSQL schema must be a nonempty identifier without NUL bytes" );

	std::string result { "\"" };
	for ( const char character : schema )
	{
		if ( character == '"' ) result.push_back( '"' );
		result.push_back( character );
	}
	result.push_back( '"' );

	if ( schema != EXTENSION_SCHEMA )
	{
		result.push_back( ',' );
		result.append( EXTENSION_SCHEMA );
	}

	return result;
}

} // namespace idhan::db
