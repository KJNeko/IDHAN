#include "urls.hpp"

#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"

namespace idhan::helpers
{
std::string extractDomain( const std::string& url )
{
	const auto protocol_end { url.find( "://" ) };
	const auto protocol { url.substr( 0, protocol_end ) };

	if ( protocol == "file" ) return "";

	const auto start_pos { protocol_end != std::string::npos ? protocol_end + 3 : 0 };
	const auto authority_end { url.find_first_of( "/?#", start_pos ) };
	std::string authority {
		authority_end != std::string::npos ?
			url.substr( start_pos, authority_end - start_pos ) :
			url.substr( start_pos )
	};

	const auto user_info_end { authority.rfind( '@' ) };
	if ( user_info_end != std::string::npos ) authority.erase( 0, user_info_end + 1 );

	if ( !authority.empty() && authority[ 0 ] == '[' )
	{
		// IPv6 literal: "[::1]:8080" → "::1"
		const auto close_bracket { authority.find( ']' ) };
		return close_bracket != std::string::npos ? authority.substr( 1, close_bracket - 1 ) : authority.substr( 1 );
	}

	const auto port_pos { authority.find( ':' ) };
	if ( port_pos != std::string::npos ) authority.erase( port_pos );

	return authority;
}

drogon::Task< std::optional< UrlID > > findUrl( const std::string& url, DbClientPtr db )
{
	const auto rows { co_await db->execSqlCoro( "SELECT url_id FROM urls WHERE url = $1", url ) };

	if ( rows.empty() ) co_return std::nullopt;

	co_return rows[ 0 ][ 0 ].as< UrlID >();
}

drogon::Task< std::optional< UrlDomainID > > findUrlDomain( const std::string& domain, DbClientPtr db )
{
	const auto rows {
		co_await db->execSqlCoro( "SELECT url_domain_id FROM url_domains WHERE url_domain = $1", domain )
	};

	if ( rows.empty() ) co_return std::nullopt;

	co_return rows[ 0 ][ 0 ].as< UrlDomainID >();
}

ExpectedTask< UrlDomainID > findOrCreateUrl( const std::string url, DbClientPtr db )
{
	const auto existing { co_await findUrl( url, db ) };

	if ( existing ) [[unlikely]]
		co_return *existing;

	const auto url_domain_id { co_await helpers::findOrCreateUrlDomain( url, db ) };
	return_unexpected_error( url_domain_id );

	const auto insert { co_await db->execSqlCoro(
		"INSERT INTO urls (url, url_domain_id) VALUES ($1, $2) ON CONFLICT DO NOTHING RETURNING url_id",
		url,
		*url_domain_id ) };

	if ( !insert.empty() ) [[likely]]
		co_return insert[ 0 ][ 0 ].as< UrlID >();

	const auto created_by_other_request { co_await findUrl( url, db ) };

	if ( created_by_other_request ) [[likely]]
		co_return *created_by_other_request;

	co_return std::unexpected( createInternalError( "Was unable to create or find url {}", url ) );
}

ExpectedTask< UrlDomainID > findOrCreateUrlDomain( const std::string url, DbClientPtr db )
{
	const std::string domain { extractDomain( url ) };

	const auto existing { co_await findUrlDomain( domain, db ) };

	if ( existing ) [[unlikely]]
		co_return *existing;

	const auto insert { co_await db->execSqlCoro(
		"INSERT INTO url_domains (url_domain) VALUES ($1) ON CONFLICT DO NOTHING RETURNING url_domain_id", domain ) };

	if ( !insert.empty() ) [[likely]]
		co_return insert[ 0 ][ 0 ].as< UrlDomainID >();

	const auto created_by_other_request { co_await findUrlDomain( domain, db ) };

	if ( created_by_other_request ) [[likely]]
		co_return *created_by_other_request;

	co_return std::unexpected( createInternalError( "Failed to create URL domain" ) );
}

ExpectedTask< void > associateUrls( const RecordID record_id, std::vector< std::string > urls, DbClientPtr db )
{
	if ( urls.empty() ) co_return {};

	std::vector< std::string > domains {};
	domains.reserve( urls.size() );
	for ( const auto& url : urls ) domains.emplace_back( extractDomain( url ) );

	co_await db->execSqlCoro(
		"INSERT INTO url_domains (url_domain) SELECT DISTINCT unnest($1::text[]) "
		"ON CONFLICT (url_domain) DO NOTHING",
		std::vector< std::string >( domains ) );
	co_await db->execSqlCoro(
		"INSERT INTO urls (url, url_domain_id) "
		"SELECT pairs.url, ud.url_domain_id "
		"FROM unnest($1::text[], $2::text[]) AS pairs(url, domain) "
		"JOIN url_domains ud ON ud.url_domain = pairs.domain "
		"ON CONFLICT (url) DO NOTHING",
		std::vector< std::string >( urls ),
		std::move( domains ) );
	co_await db->execSqlCoro(
		"INSERT INTO url_mappings (url_id, record_id) "
		"SELECT url_id, $2 FROM urls WHERE url = ANY($1::text[]) ON CONFLICT DO NOTHING",
		std::move( urls ),
		record_id );
	co_return {};
}

} // namespace idhan::helpers
