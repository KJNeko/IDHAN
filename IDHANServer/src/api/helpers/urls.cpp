//
// Created by kj16609 on 7/24/25.
//

#include "urls.hpp"

#include "createBadRequest.hpp"

namespace idhan::helpers
{
ExpectedTask< UrlDomainID > findOrCreateUrl( const std::string url, DbClientPtr db )
{
	const auto search_result { co_await db->execSqlCoro( "SELECT url_id FROM urls WHERE url = $1", url ) };

	if ( !search_result.empty() ) [[unlikely]]
		co_return search_result[ 0 ][ 0 ].as< UrlID >();

	const auto url_domain_id { co_await helpers::findOrCreateUrlDomain( url, db ) };
	return_unexpected_error( url_domain_id );

	const auto insert { co_await db->execSqlCoro(
		"INSERT INTO urls (url, url_domain_id) VALUES ($1, $2) ON CONFLICT DO NOTHING RETURNING url_id",
		url,
		*url_domain_id ) };

	if ( !insert.empty() ) [[likely]]
		co_return insert[ 0 ][ 0 ].as< UrlID >();

	const auto second_search { co_await db->execSqlCoro( "SELECT url_id FROM urls WHERE url = $1", url ) };

	if ( !second_search.empty() ) [[likely]]
		co_return second_search[ 0 ][ 0 ].as< UrlID >();

	co_return std::unexpected( createInternalError( "Was unable to create or find url {}", url ) );
}

ExpectedTask< UrlDomainID > findOrCreateUrlDomain( const std::string url, DbClientPtr db )
{
	// Extract domain from URL (netloc/host part)
	const auto protocol_end = url.find( "://" );
	const auto start_pos = protocol_end != std::string::npos ? protocol_end + 3 : 0;
	const auto path_start = url.find( '/', start_pos );
	std::string domain {
		path_start != std::string::npos ? url.substr( start_pos, path_start - start_pos ) : url.substr( start_pos )
	};

	// Remove port if present
	const auto port_pos = domain.find( ':' );
	if ( port_pos != std::string::npos ) domain = domain.substr( 0, port_pos );

	const auto search_result {
		co_await db->execSqlCoro( "SELECT url_domain_id FROM url_domains WHERE url_domain = $1", domain )
	};

	if ( !search_result.empty() ) [[unlikely]]
		co_return search_result[ 0 ][ 0 ].as< UrlDomainID >();

	const auto insert { co_await db->execSqlCoro(
		"INSERT INTO url_domains (url_domain) VALUES ($1) ON CONFLICT DO NOTHING RETURNING url_domain_id", domain ) };

	if ( !insert.empty() ) [[likely]]
		co_return insert[ 0 ][ 0 ].as< UrlDomainID >();

	const auto second_search_result {
		co_await db->execSqlCoro( "SELECT url_domain_id FROM url_domains WHERE url_domain = $1", domain )
	};

	if ( !second_search_result.empty() ) [[likely]]
		co_return second_search_result[ 0 ][ 0 ].as< UrlDomainID >();

	co_return std::unexpected( createInternalError( "Failed to create URL domain" ) );
}

} // namespace idhan::helpers
