//
// Created by kj16609 on 11/14/24.
//

#include "IDHANTagAPI.hpp"
#include "logging/log.hpp"

namespace idhan::api
{
drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::
	search( drogon::HttpRequestPtr request, const std::string tag_text )
{
	auto db { drogon::app().getDbClient() };

	const auto result { co_await db->execSqlCoro( "SELECT tag_id FROM tags_combined WHERE tag_text = $1", tag_text ) };

	Json::Value json {};

	if ( result.size() > 0 )
	{
		const auto tag_id { result[ 0 ][ 0 ].as< TagID >() };
		log::debug( "Result of /tag/search?tag={}: {}", tag_text, tag_id );
		json[ "tag_id" ] = tag_id;
		json[ "found" ] = true;
	}
	else
		json[ "found" ] = false;

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api
