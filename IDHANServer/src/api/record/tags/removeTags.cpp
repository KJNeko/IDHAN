//
// Created by kj16609 on 3/11/25.
//

#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "IDHANTypes.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::removeTags(
	const drogon::HttpRequestPtr request,
	const RecordID record_id )
{
	const auto json_obj { request->getJsonObject() };

	if ( json_obj == nullptr ) co_return createBadRequest( "No valid json object" );

	const auto& json { *json_obj };

	if ( !json.isArray() ) co_return createBadRequest( "Invalid json: Expected array of tag_ids" );

	const auto tag_domain_id { helpers::getTagDomainIDParameter( request ) };
	if ( !tag_domain_id ) co_return tag_domain_id.error();

	std::vector< TagID > tag_ids {};
	tag_ids.reserve( json.size() );

	for ( const auto& item : json )
	{
		if ( !item.isIntegral() ) co_return createBadRequest( "Invalid tag_id in array: Must be integral" );
		tag_ids.push_back( item.as< TagID >() );
	}

	if ( tag_ids.empty() ) co_return drogon::HttpResponse::newHttpResponse();

	auto db { drogon::app().getDbClient() };

	try
	{
		co_await db->execSqlCoro(
			"DELETE FROM tag_mappings WHERE record_id = $1 AND tag_id IN (SELECT UNNEST($2::" TAG_PG_TYPE_NAME
			"[])) AND tag_domain_id = $3",
			record_id,
			std::move( tag_ids ),
			tag_domain_id.value() );
	}
	catch ( std::exception& e )
	{
		co_return createInternalError( "Error removing tags: {}", e.what() );
	}

	Json::Value ok {};
	ok[ "status" ] = 200;

	co_return drogon::HttpResponse::newHttpJsonResponse( ok );
}

} // namespace idhan::api
