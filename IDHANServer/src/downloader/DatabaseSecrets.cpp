#include "DatabaseSecrets.hpp"

#include <drogon/drogon.h>

#include <vector>

#include "db/drogonArrayBind.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"

namespace idhan::downloader
{

std::optional< std::string > DatabaseSecrets::secret( const std::string_view name )
{
	std::optional< std::string > value {};

	try
	{
		drogon::sync_wait(
			[ key = std::string { name }, &value ]() -> drogon::Task< void >
			{
				const auto rows { co_await drogon::app().getDbClient()->execSqlCoro(
					"SELECT value FROM downloader_secrets WHERE name = $1", key ) };

				if ( rows.size() > 0 ) value = rows[ 0 ][ "value" ].as< std::string >();
			}() );
	}
	catch ( const std::exception& e )
	{
		log::warn( "Unable to read downloader secret {}: {}", name, e.what() );
		return std::nullopt;
	}

	return value;
}

drogon::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > DatabaseSecrets::values()
	const
{
	std::unordered_map< std::string, std::string > values {};

	try
	{
		const auto rows {
			co_await drogon::app().getDbClient()->execSqlCoro( "SELECT name, value FROM downloader_secrets" )
		};

		for ( const auto& row : rows )
			values.emplace( row[ "name" ].as< std::string >(), row[ "value" ].as< std::string >() );
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( format_ns::format( "Unable to read downloader secrets: {}", e.what() ) );
	}

	co_return values;
}

drogon::Task< std::expected< void, std::string > > DatabaseSecrets::set(
	std::unordered_map< std::string, std::string > values )
{
	std::vector< std::string > names {};
	std::vector< std::string > secrets {};
	names.reserve( values.size() );
	secrets.reserve( values.size() );

	for ( auto& [ name, value ] : values )
	{
		names.emplace_back( name );
		secrets.emplace_back( std::move( value ) );
	}

	try
	{
		co_await drogon::app().getDbClient()->execSqlCoro(
			"INSERT INTO downloader_secrets (name, value) SELECT * FROM UNNEST($1::text[], $2::text[]) "
			"ON CONFLICT (name) DO UPDATE SET value = EXCLUDED.value",
			std::move( names ),
			std::move( secrets ) );
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( format_ns::format( "Unable to store downloader secrets: {}", e.what() ) );
	}

	co_return std::expected< void, std::string > {};
}

} // namespace idhan::downloader
