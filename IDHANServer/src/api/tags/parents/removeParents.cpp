#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::removeTagParents( const drogon::HttpRequestPtr request )
{
	const auto json_obj { request->getJsonObject() };

	if ( json_obj == nullptr ) co_return createBadRequest( "No valid json object" );

	const auto json { *json_obj };

	if ( !json.isArray() ) co_return createBadRequest( "Invalid json object. Expected array as root item" );

	const auto db { drogon::app().getDbClient() };
	const auto tag_domain_id { helpers::getTagDomainIDParameter( request ) };

	if ( !tag_domain_id ) co_return tag_domain_id.error();

	for ( const auto& item : json )
	{
		const auto& parent { item[ "parent_id" ] };
		const auto& child { item[ "child_id" ] };

		if ( !parent.isIntegral() ) co_return createBadRequest( "Invalid parent item: Must be in TagID form" );
		if ( !child.isIntegral() ) co_return createBadRequest( "Invalid child item: Must be in TagID form" );

		const TagID parent_id { parent.as< TagID >() };
		const TagID child_id { child.as< TagID >() };

		try
		{
			co_await db->execSqlCoro(
				"DELETE FROM tag_parents WHERE tag_domain_id = $1 AND parent_id = $2 AND child_id = $3",
				tag_domain_id.value(),
				parent_id,
				child_id );
		}
		catch ( std::exception& e )
		{
			co_return createInternalError( "Error removing parent relationship: {}", e.what() );
		}
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
