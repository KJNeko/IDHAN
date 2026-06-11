#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::removeTagSiblings( const drogon::HttpRequestPtr request )
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
		const auto& older { item[ "older_id" ] };
		const auto& younger { item[ "younger_id" ] };

		if ( !older.isIntegral() ) co_return createBadRequest( "Invalid older item: Must be in TagID form" );
		if ( !younger.isIntegral() ) co_return createBadRequest( "Invalid younger item: Must be in TagID form" );

		const TagID older_id { older.as< TagID >() };
		const TagID younger_id { younger.as< TagID >() };

		try
		{
			co_await db->execSqlCoro(
				"DELETE FROM tag_siblings WHERE tag_domain_id = $1 AND older_id = $2 AND younger_id = $3",
				tag_domain_id.value(),
				older_id,
				younger_id );
		}
		catch ( std::exception& e )
		{
			co_return createInternalError( "Error removing sibling relationship: {}", e.what() );
		}
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
