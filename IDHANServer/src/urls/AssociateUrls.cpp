#include "AssociateUrls.hpp"

#include <map>
#include <utility>

#include "UrlDomain.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::helpers
{

runtime::Task< std::expected< void, std::string > > setUrlTypes(
	std::vector< std::pair< std::string, std::string > > types,
	db::DBContext& db )
{
	try
	{
		std::map< std::string, std::string > unique;
		for ( auto& [ url, type ] : types ) unique[ std::move( url ) ] = std::move( type );

		std::vector< std::string > urls, domains, values;
		for ( const auto& [ url, type ] : unique )
		{
			urls.push_back( url );
			domains.push_back( extractDomain( url ) );
			values.push_back( type );
		}
		if ( urls.empty() ) co_return std::expected< void, std::string > {};

		db::DBBindings bindings;
		bindings.bind( urls );
		bindings.bind( domains );
		bindings.bind( values );
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					WITH input AS MATERIALIZED (
					    SELECT
					        *
					    FROM unnest(
					            $1::text[], $2::text[], $3::text[]
					    ) AS i (
					            url,
					            domain,
					            type
					    )
					),
					domains AS (
					    INSERT INTO url_domains (url_domain)
					    SELECT DISTINCT
					        domain
					    FROM input
					    ON CONFLICT (url_domain)
					        DO UPDATE SET
					            url_domain = EXCLUDED.url_domain
					        RETURNING url_domain_id, url_domain
					)
					INSERT INTO urls (url, url_domain_id, url_type)
					SELECT
					    i.url,
					    d.url_domain_id,
					    i.type::UrlType FROM input i
					    JOIN domains d ON d.url_domain = i.domain
					ON CONFLICT (url)
					    DO UPDATE SET
					        url_type = EXCLUDED.url_type
				)SQL",
				std::move( bindings ) )
			.result();
		co_return std::expected< void, std::string > {};
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( std::string { e.what() } );
	}
}

runtime::Task< std::expected< void, std::string > > setUrlType( std::string url, std::string type, db::DBContext& db )
{
	try
	{
		db::DBBindings bindings {};
		bindings.bind( url );
		bindings.bind( extractDomain( url ) );
		bindings.bind( type );
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					WITH domain AS (
					    INSERT INTO url_domains (url_domain)
					        VALUES ($2)
					    ON CONFLICT (url_domain)
					        DO UPDATE SET
					            url_domain = EXCLUDED.url_domain
					        RETURNING url_domain_id
					)
					INSERT INTO urls (url, url_domain_id, url_type)
					SELECT
					    $1,
					    url_domain_id,
					    $3::UrlType FROM domain
					ON CONFLICT (url)
					    DO UPDATE SET
					        url_type = EXCLUDED.url_type
				)SQL",
				std::move( bindings ) )
			.result();
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}

	co_return std::expected< void, std::string > {};
}

runtime::Task< std::expected< void, std::string > > associateUrls(
	const RecordID record_id,
	std::vector< std::string > urls,
	db::DBContext& db )
{
	if ( urls.empty() ) co_return std::expected< void, std::string > {};

	std::vector< std::string > domains {};
	domains.reserve( urls.size() );
	for ( const auto& url : urls ) domains.emplace_back( extractDomain( url ) );

	try
	{
		db::DBBindings domain_bindings {};
		domain_bindings.bind( domains );
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					INSERT INTO url_domains (url_domain)
					SELECT DISTINCT
					    unnest($1::text[])
					ON CONFLICT (url_domain)
					    DO NOTHING
				)SQL",
				std::move( domain_bindings ) )
			.result();

		db::DBBindings url_bindings {};
		url_bindings.bind( urls );
		url_bindings.bind( domains );
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					INSERT INTO urls (url, url_domain_id)
					SELECT
					    pairs.url,
					    ud.url_domain_id
					FROM unnest($1::text[], $2::text[]) AS pairs (url,
					        domain)
					    JOIN url_domains ud ON ud.url_domain = pairs.domain
					ON CONFLICT (url)
					    DO NOTHING
				)SQL",
				std::move( url_bindings ) )
			.result();

		db::DBBindings mapping_bindings {};
		mapping_bindings.bind( urls );
		mapping_bindings.bind( record_id );
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					INSERT INTO url_mappings (url_id, record_id)
					SELECT
					    url_id,
					    $2
					FROM urls
					WHERE url = ANY ($1::text[])
					ON CONFLICT
					    DO NOTHING
				)SQL",
				std::move( mapping_bindings ) )
			.result();
	}
	catch ( const std::exception& error )
	{
		co_return std::unexpected( std::string { error.what() } );
	}

	co_return std::expected< void, std::string > {};
}

} // namespace idhan::helpers
