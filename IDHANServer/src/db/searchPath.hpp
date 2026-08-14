#pragma once

#include <string>
#include <string_view>

namespace idhan::db
{

//! The schema IDHAN occupies unless configured otherwise.
constexpr std::string_view DEFAULT_SCHEMA { "public" };

//! Builds the search_path that BOTH the migration connection (libpqxx) and the runtime connection
//! (drogon) must use.
[[nodiscard]] inline std::string makeSearchPath( const std::string_view schema )
{
	std::string result { DEFAULT_SCHEMA };

	if ( !schema.empty() && schema != DEFAULT_SCHEMA )
	{
		result.clear();
		result.reserve( schema.size() + 1 + DEFAULT_SCHEMA.size() );
		result.append( schema );
		result.push_back( ',' );
		result.append( DEFAULT_SCHEMA );
	}

	return result;
}

} // namespace idhan::db
