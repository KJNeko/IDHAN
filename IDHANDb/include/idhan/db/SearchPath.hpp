#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace idhan::db
{

inline constexpr std::string_view DEFAULT_SCHEMA { "idhan" };
inline constexpr std::string_view EXTENSION_SCHEMA { "public" };

// A schema setting names one identifier, even when it contains commas or quotes.
[[nodiscard]] inline std::string quoteSchemaIdentifier( const std::string_view schema )
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

	return result;
}

[[nodiscard]] inline std::string makeSearchPath(
	const std::string_view schema,
	const std::string_view extension_schema = EXTENSION_SCHEMA )
{
	std::string result { quoteSchemaIdentifier( schema ) };
	const auto extensions { quoteSchemaIdentifier( extension_schema ) };
	if ( schema != extension_schema ) result += "," + extensions;
	return result;
}

} // namespace idhan::db
