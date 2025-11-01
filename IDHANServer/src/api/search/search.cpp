//
// Created by kj16609 on 3/22/25.
//

#include <format>

#include "api/SearchAPI.hpp"
#include "api/helpers/getArrayParameters.hpp"
#include "core/search/SearchBuilder.hpp"
#include "db/TagSearch.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > SearchAPI::search( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	// Drogon does not support tag_id=1?tag_id=2 for some reason, But it's possible to be sent like that, So we'll
	// handle it here. Support both tag_id (singular) and tag_ids (plural) for compatibility
	std::vector< TagID > tag_ids { parseArrayParmeters< TagID >( request, "tag_ids" ) };
	if ( tag_ids.empty() )
	{
		tag_ids = parseArrayParmeters< TagID >( request, "tag_id" );
	}

	const auto tag_domain_ids { parseArrayParmeters< TagDomainID >( request, "tag_domains" ) };

	// const bool use_stored { request->getOptionalParameter< bool >( "use_stored" ).value_or( false ) };

	SearchBuilder builder {};

	builder.setTags( tag_ids );

	const auto result { co_await builder.query( db, tag_domain_ids ) };

	if ( result.empty() )
	{
		Json::Value root { Json::arrayValue };
		co_return drogon::HttpResponse::newHttpJsonResponse( root );
	}

	Json::Value file_ids {};

	for ( const auto& row : result )
	{
		const auto id { row[ 0 ].as< std::size_t >() };
		file_ids.append( id );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( file_ids );
}

} // namespace idhan::api
