//
// Created by kj16609 on 3/22/25.
//

#include "api/IDHANSearchAPI.hpp"
#include "api/helpers/getArrayParameters.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > IDHANSearchAPI::search( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	// Drogon does not support tag_id=1?tag_id=2 for some reason, But it's possible to be sent like that, So we'll handle it here.
	const auto tag_ids { parseArrayParmeters< std::size_t >( request, "tag_id" ) };

	const auto result { co_await db->execSqlCoro( "SELECT record_id FROM file_info" ) };

	log::info( "Returning {} results", result.size() );

	if ( result.empty() )
	{
		Json::Value root {};
		root[ "file_ids" ] = Json::arrayValue;
		co_return drogon::HttpResponse::newHttpJsonResponse( root );
	}
	else
	{
		Json::Value file_ids {};

		for ( const auto& row : result )
		{
			const auto id { row[ 0 ].as< std::size_t >() };
			file_ids.append( id );
		}

		log::info( "Returning {} results", file_ids.size() );

		Json::Value json {};
		json[ "file_ids" ] = std::move( file_ids );

		co_return drogon::HttpResponse::newHttpJsonResponse( json );
	}
}

} // namespace idhan::api