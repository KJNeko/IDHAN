//
// Created by kj16609 on 7/24/25.
//

#include "urls.hpp"

#include "createBadRequest.hpp"

namespace idhan::helpers
{
drogon::Task< std::expected< UrlID, drogon::HttpResponsePtr > > findOrCreateUrl( const std::string url, DbClientPtr db )
{
	UrlID url_id { INVALID_URL_ID };
	std::size_t tries { 0 };

	do
	{
		tries += 1;
		if ( tries > 16 ) co_return std::unexpected( createBadRequest( "Too many URL creation attempts" ) );
		const auto search_result { co_await db->execSqlCoro( "SELECT url_id FROM urls WHERE url = $1", url ) };

		if ( !search_result.empty() )
		{
			url_id = search_result[ 0 ][ 0 ].as< UrlID >();
			break;
		}

		const auto insert { co_await db->execSqlCoro(
			"INSERT INTO urls (url) VALUES ($1) ON CONFLICT DO NOTHING RETURNING url_id", url ) };

		if ( !insert.empty() ) url_id = insert[ 0 ][ 0 ].as< UrlID >();
	}
	while ( url_id == INVALID_URL_ID );

	co_return url_id;
}
} // namespace idhan::helpers
