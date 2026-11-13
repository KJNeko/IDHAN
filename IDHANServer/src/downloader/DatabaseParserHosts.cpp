#include "DatabaseParserHosts.hpp"

#include <ada.h>
#include "idhan/db/DBContext.hpp"

namespace idhan::downloader
{

std::expected< std::string, std::string > normalizeParserHost( const std::string_view host )
{
	if ( host.empty() || host.size() > 253 ) return std::unexpected( "Host must contain 1 to 253 characters" );


	if ( host.find_first_of( "/\\?#@ \t\r\n" ) != std::string_view::npos )
		return std::unexpected( "Host must be a bare hostname" );


	if ( host.contains( ':' ) && !host.ends_with( ']' ) )
		return std::unexpected( "Host must not include a port" );


	const auto parsed_e { ada::parse< ada::url_aggregator >( "https://" + std::string { host } + "/" ) };
	if ( !parsed_e.has_value() ) return std::unexpected( "Invalid hostname" );


	std::string normalized { parsed_e.value().get_hostname() };
	if ( normalized.ends_with( '.' ) ) normalized.pop_back();

	if ( normalized.empty() ) return std::unexpected( "Invalid hostname" );


	return normalized;
}

runtime::Task< std::vector< ParserHost > > DatabaseParserHosts::list( db::DBContext& db )
{
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    host,
					    parser,
					    auto_discovered
					FROM downloader_parser_hosts
					ORDER BY
					    parser,
					    host
				)SQL" )
				 .rows< std::string, std::string, bool >()
	};
	std::vector< ParserHost > hosts;
	for ( const auto& [ host, parser, discovered ] : rows ) hosts.push_back( { host, parser, discovered } );

	co_return hosts;
}

runtime::Task< ParserHost > DatabaseParserHosts::upsert( db::DBContext& db, ParserHost entry )
{
	db::DBBindings bindings;
	bindings.bind( entry.host );
	bindings.bind( entry.parser );
	bindings.bind( entry.auto_discovered );
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					INSERT INTO downloader_parser_hosts AS stored (host, parser, auto_discovered)
					    VALUES ($1, $2, $3)
					ON CONFLICT (host)
					    DO UPDATE SET
					        parser = CASE WHEN EXCLUDED.auto_discovered THEN stored.parser
					        ELSE
					            EXCLUDED.parser
					        END,
					        auto_discovered = stored.auto_discovered
					        AND EXCLUDED.auto_discovered
					    RETURNING host,
					        parser,
					        auto_discovered
				)SQL",
					 std::move( bindings ) )
				 .rows< std::string, std::string, bool >()
	};
	const auto& [ host, parser, discovered ] { rows.row( 0 ) };
	co_return ParserHost { host, parser, discovered };
}

runtime::Task< bool > DatabaseParserHosts::erase( db::DBContext& db, std::string host )
{
	db::DBBindings bindings;
	bindings.bind( std::move( host ) );
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					DELETE FROM downloader_parser_hosts
					WHERE host = $1
					RETURNING host
				)SQL",
					 std::move( bindings ) )
				 .rows< std::string >()
	};
	co_return !rows.empty();
}

folly::coro::Task< std::expected< std::vector< ParserHost >, std::string > > DatabaseParserHosts::loadAll()
{
	try
	{
		co_return co_await list( m_db );
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( std::string { e.what() } );
	}
}

folly::coro::Task< std::expected< ParserHost, std::string > > DatabaseParserHosts::discover(
	std::string host,
	std::string parser )
{
	try
	{
		co_return co_await upsert( m_db, { std::move( host ), std::move( parser ), true } );
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}
}

} // namespace idhan::downloader
