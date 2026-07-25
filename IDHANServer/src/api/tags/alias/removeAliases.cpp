#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "logging/log.hpp"
#include "profiling/tracy.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::removeTagAliases( const drogon::HttpRequestPtr request )
{
	ZoneScopedN( "TagAPI::removeTagAliases" );
	const auto json_obj { request->getJsonObject() };

	if ( json_obj == nullptr ) co_return createBadRequest( "No valid json object" );

	const auto json { *json_obj };

	if ( !json.isArray() ) co_return createBadRequest( "Invalid json object. Expected array as root item" );

	const auto db { drogon::app().getDbClient() };
	const auto tag_domain_id { helpers::getTagDomainIDParameter( request ) };

	if ( !tag_domain_id ) co_return tag_domain_id.error();

	for ( const auto& item : json )
	{
		const auto& aliased { item[ "aliased_id" ] };
		const auto& alias { item[ "alias_id" ] };

		if ( !aliased.isIntegral() ) co_return createBadRequest( "Invalid aliased item: Must be in TagID form" );
		if ( !alias.isIntegral() ) co_return createBadRequest( "Invalid alias item: Must be in TagID form" );

		const TagID aliased_id { aliased.as< TagID >() };
		const TagID alias_id { alias.as< TagID >() };

		try
		{
			co_await db->execSqlCoro(
				"DELETE FROM tag_aliases WHERE tag_domain_id = $1 AND aliased_id = $2 AND alias_id = $3",
				tag_domain_id.value(),
				aliased_id,
				alias_id );
		}
		catch ( std::exception& e )
		{
			co_return createInternalError( "Error removing alias relationship: {}", e.what() );
		}
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
