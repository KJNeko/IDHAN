//
// Created by kj16609 on 3/22/25.
//

#include <format>

#include "api/SearchAPI.hpp"
#include "api/helpers/getArrayParameters.hpp"
#include "db/TagSearch.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > SearchAPI::search( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	// Drogon does not support tag_id=1?tag_id=2 for some reason, But it's possible to be sent like that, So we'll handle it here.
	// Support both tag_id (singular) and tag_ids (plural) for compatibility
	auto tag_ids { parseArrayParmeters< std::size_t >( request, "tag_ids" ) };
	if ( tag_ids.empty() )
	{
		tag_ids = parseArrayParmeters< std::size_t >( request, "tag_id" );
	}
	const auto tag_domain_ids { parseArrayParmeters< TagDomainID >( request, "tag_domains" ) };

	const bool use_stored { request->getOptionalParameter< bool >( "use_stored" ).value_or( false ) };

	std::string query {
		"SELECT record_id, count(*) AS count FROM active_tag_mappings WHERE tag_id = ANY($1) and tag_domain_id = ANY($2) GROUP BY record_id, tag_domain_id"
	};

	std::string table_source {};
	if ( use_stored )
		table_source = "active_tag_mappings";
	else
		table_source = "active_tag_mappings_final";

	// remove the filter for the domain_id as it's not needed here, we want all domains
	if ( tag_domain_ids.empty() )
	{
		query =
			"SELECT record_id, count(*) as count FROM active_tag_mappings WHERE tag_id = ANY($1) GROUP BY record_id, tag_domain_id";
	}

	log::info( "Query: {}", query );

	//TODO: Search for multiple tags
	const auto result { co_await db->execSqlCoro( query, static_cast< TagID >( tag_ids[ 0 ] ) ) };

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