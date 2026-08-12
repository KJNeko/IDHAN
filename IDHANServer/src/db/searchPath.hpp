#pragma once

#include <string>
#include <string_view>

namespace idhan::db
{

//! The schema IDHAN occupies unless configured otherwise.
constexpr std::string_view DEFAULT_SCHEMA { "public" };

//! Builds the search_path that BOTH the migration connection (libpqxx) and the runtime connection
//! (drogon) must use.
/** Always ends in public. CREATE EXTENSION installs into a single schema for the whole database, so
 *  a server occupying any other schema still has to see it.
 *
 *  Deriving this in one place is the entire point of the function. The two connection sites
 *  previously spelled the path out independently and drifted apart -- migrations ran with
 *  `test,public` while the running server used `test` -- which left objects that migrations could
 *  resolve and the server could not, failing only under test. */
[[nodiscard]] inline std::string makeSearchPath( const std::string_view schema )
{
	// One named object and one return, so NRVO applies. Returning a different object from an early
	// exit would disable it and trip -Wnrvo under this project's warning set.
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
