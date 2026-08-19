#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace idhan::test
{

inline std::string envOr( const char* const name, const std::string_view fallback )
{
	const char* const value { std::getenv( name ) };
	return value == nullptr ? std::string( fallback ) : std::string( value );
}

//! Where the tests expect postgres to be. Overridable so a CI runner can point elsewhere.
struct PostgresSettings
{
	std::string host { envOr( "IDHAN_TEST_PG_HOST", "localhost" ) };
	std::string port { envOr( "IDHAN_TEST_PG_PORT", "5432" ) };
	std::string database { envOr( "IDHAN_TEST_PG_DB", "idhan-db" ) };
	std::string user { envOr( "IDHAN_TEST_PG_USER", "idhan" ) };
	std::string password { envOr( "IDHAN_TEST_PG_PASSWORD", "idhan" ) };

	[[nodiscard]] std::string connectionString() const
	{
		return "host=" + host + " port=" + port + " dbname=" + database + " user=" + user + " password=" + password;
	}
};

inline std::string connectionString()
{
	return PostgresSettings {}.connectionString();
}

} // namespace idhan::test
