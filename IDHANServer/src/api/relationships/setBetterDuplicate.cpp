//
// Created by kj16609 on 11/5/25.
//

#include "IDHANTypes.hpp"
#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > setBetterDuplicateMultiple( const Json::Value json )
{
	auto db { drogon::app().getDbClient() };

	for ( const auto& object : json )
	{
		if ( !object.isMember( "worse_id" ) || !object.isMember( "better_id" ) )
			co_return createBadRequest( "Expected object to have `worse_id` and `better_id`" );

		if ( !object[ "worse_id" ].isUInt() || !object[ "better_id" ].isUInt() )
			co_return createBadRequest( "Expected `worse_id` and `better_id` to be unsigned integers" );

		const RecordID worse_id { object[ "worse_id" ].as< RecordID >() };
		const RecordID better_id { object[ "better_id" ].as< RecordID >() };

		co_await db->execSqlCoro( "SELECT insert_duplicate_pair($1, $2)", worse_id, better_id );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( {} );
}

drogon::Task< drogon::HttpResponsePtr > setBetterDuplicateSingle( const Json::Value json )
{
	auto db { drogon::app().getDbClient() };

	if ( !json[ "worse_id" ].isUInt() || !json[ "better_id" ].isUInt() )
		co_return createBadRequest( "Expected `worse_id` and `better_id` to be unsigned integers" );

	const RecordID worse_id { json[ "worse_id" ].as< RecordID >() };
	const RecordID better_id { json[ "better_id" ].as< RecordID >() };

	co_await db->execSqlCoro( "SELECT insert_duplicate_pair($1, $2)", worse_id, better_id );

	co_return drogon::HttpResponse::newHttpJsonResponse( {} );
}

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::setBetterDuplicate( const drogon::HttpRequestPtr request )
{
	const auto json_ptr { request->getJsonObject() };
	if ( !json_ptr ) co_return createBadRequest( "Expected json body" );
	const auto& json { *json_ptr };

	if ( json.isArray() ) co_return co_await setBetterDuplicateMultiple( json );
	if ( json.isObject() ) co_return co_await setBetterDuplicateSingle( json );

	co_return createBadRequest(
		"Expected json body of either array of objects, or a single object. Objects must have `better_id` and `worse_id`" );
}

} // namespace idhan::api