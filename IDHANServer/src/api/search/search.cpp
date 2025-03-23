//
// Created by kj16609 on 3/22/25.
//

#include "api/IDHANSearchAPI.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > IDHANSearchAPI::search( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	const auto result { co_await db->execSqlCoro( "SELECT record_id FROM file_info" ) };

	log::info( "Returning {} results", result.size() );

	Json::Value file_ids {};

	for ( const auto& row : result )
	{
		const auto id { row[ 0 ].as< std::size_t >() };
		file_ids.append( id );
	}

	log::info( "Returning {} results", file_ids.size() );

	Json::Value json {};
	json[ "file_ids" ] = file_ids;

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api