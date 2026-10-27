#include "DatabaseSecrets.hpp"

#include <folly/coro/BlockingWait.h>

#include <vector>

#include "DownloadSessionManager.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/format_ns.hpp"
#include "logging/log.hpp"

namespace idhan::downloader
{

std::optional< std::string > DatabaseSecrets::secret( const std::string_view name )
{
	std::optional< std::string > value {};

	try
	{
		db::DBBindings bindings {};
		bindings.bind( std::string { name } );

		auto rows { folly::coro::blockingWait(
			DownloadSessionManager::dbContext()
				.query( "SELECT value FROM downloader_secrets WHERE name = $1", std::move( bindings ) )
				.rows< std::string >() ) };

		if ( !rows.empty() ) value = std::get< 0 >( rows.row( 0 ) );
	}
	catch ( const std::exception& e )
	{
		log::warn( "Unable to read downloader secret {}: {}", name, e.what() );
		value.reset();
	}

	return value;
}

runtime::Task< std::expected< std::unordered_map< std::string, std::string >, std::string > > DatabaseSecrets::values()
	const
{
	std::unordered_map< std::string, std::string > values {};

	try
	{
		const auto rows {
			co_await DownloadSessionManager::dbContext()
				.query( "SELECT name, value FROM downloader_secrets" )
				.rows< std::string, std::string >()
		};

		for ( auto [ name, value ] : rows ) values.emplace( std::move( name ), std::move( value ) );
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( format_ns::format( "Unable to read downloader secrets: {}", e.what() ) );
	}

	co_return values;
}

runtime::Task< std::expected< void, std::string > > DatabaseSecrets::set(
	std::unordered_map< std::string, std::string > values )
{
	try
	{
		std::vector< std::string > names {};
		std::vector< std::string > secrets {};
		names.reserve( values.size() );
		secrets.reserve( values.size() );

		for ( auto& [ name, value ] : values )
		{
			names.emplace_back( std::move( name ) );
			secrets.emplace_back( std::move( value ) );
		}

		db::DBBindings bindings {};
		bindings.bind( names );
		bindings.bind( secrets );
		co_await DownloadSessionManager::dbContext().query(
			"INSERT INTO downloader_secrets (name, value) SELECT * FROM UNNEST($1::text[], $2::text[]) "
			"ON CONFLICT (name) DO UPDATE SET value = EXCLUDED.value",
			std::move( bindings ) );
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( format_ns::format( "Unable to store downloader secrets: {}", e.what() ) );
	}

	co_return std::expected< void, std::string > {};
}

} // namespace idhan::downloader
