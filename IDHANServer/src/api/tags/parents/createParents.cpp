//
// Created by kj16609 on 3/11/25.
//

#include <expected>

#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::createTagParents( drogon::HttpRequestPtr request )
{
	const auto json_obj { request->getJsonObject() };

	if ( json_obj == nullptr )
	{
		co_return createBadRequest( "No valid json object" );
	}

	const auto json { *json_obj };

	if ( !json.isArray() )
	{
		co_return createBadRequest( "Invalid json object. Expected array as root item" );
	}

	const auto db { drogon::app().getDbClient() };

	const auto tag_domain_id { helpers::getTagDomainID( request ) };

	if ( !tag_domain_id ) co_return tag_domain_id.error();

	for ( const auto& item : json )
	{
		const auto& parent { item[ "parent_id" ] };
		const auto& child { item[ "child_id" ] };

		if ( !parent.isIntegral() ) co_return createBadRequest( "Invalid parent item: Must be in TagID form" );
		if ( !child.isIntegral() ) co_return createBadRequest( "Invalid child item: Must be in TagID form" );

		const TagID parent_id { parent.as< TagID >() };
		const TagID child_id { child.as< TagID >() };

		co_await db->execSqlCoro(
			"INSERT INTO tag_parents (domain_id, parent_id, child_id) VALUES ($1, $2, $3) ON CONFLICT(domain_id, parent_id, child_id) DO NOTHING",
			tag_domain_id.value(),
			parent_id,
			child_id );
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api