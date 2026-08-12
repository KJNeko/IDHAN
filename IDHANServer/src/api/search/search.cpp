#include <format>

#include "api/SearchAPI.hpp"
#include "api/helpers/getArrayParameters.hpp"
#include "api/search/parseSortType.hpp"
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
	std::vector< TagID > tag_ids { parseArrayParameters< TagID >( request, "tag_ids" ) };
	if ( tag_ids.empty() )
	{
		tag_ids = parseArrayParameters< TagID >( request, "tag_id" );
	}

	const auto tag_domain_ids { parseArrayParameters< TagDomainID >( request, "tag_domains" ) };

	// const bool use_stored { request->getOptionalParameter< bool >( "use_stored" ).value_or( false ) };

	SearchBuilder builder {};

	builder.addPositiveTags( tag_ids );

	// Sort params are optional; the default (filesize ascending) matches this endpoint's
	// long-standing documented behavior, so omitting them changes nothing for existing callers.
	const auto by { request->getOptionalParameter< std::string >( "by" ) };
	const auto order { request->getOptionalParameter< std::string >( "order" ) };
	if ( by ) builder.setSortType( parseSortType( *by ) );
	if ( order ) builder.setSortOrder( *order == "desc" ? SortOrder::DESC : SortOrder::ASC );

	const auto result { co_await builder.query( db, tag_domain_ids ) };

	// arrayValue explicitly: a default-constructed Json::Value is null until first append, so an
	// empty result would otherwise serialise as `null` rather than `[]`.
	Json::Value file_ids { Json::arrayValue };

	for ( const auto id : result.record_ids ) file_ids.append( id );

	co_return drogon::HttpResponse::newHttpJsonResponse( file_ids );
}

} // namespace idhan::api
