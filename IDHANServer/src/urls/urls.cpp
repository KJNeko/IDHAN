#include "urls.hpp"

#include "api/helpers/createBadRequest.hpp"

namespace idhan::helpers
{
std::string extractDomain( const std::string& url )
{
	const auto protocol_end { url.find( "://" ) };
	const auto start_pos { protocol_end != std::string::npos ? protocol_end + 3 : 0 };
	const auto path_start { url.find( '/', start_pos ) };
	std::string domain {
		path_start != std::string::npos ? url.substr( start_pos, path_start - start_pos ) : url.substr( start_pos )
	};
	if ( !domain.empty() && domain[ 0 ] == '[' )
	{
		// IPv6 literal: "[::1]:8080" → "::1"
		const auto close_bracket { domain.find( ']' ) };
		domain = ( close_bracket != std::string::npos ) ? domain.substr( 1, close_bracket - 1 ) : domain.substr( 1 );
	}
	else if ( const auto port_pos { domain.find( ':' ) }; port_pos != std::string::npos )
	{
		domain = domain.substr( 0, port_pos );
	}
	return domain;
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

} // namespace idhan::helpers
