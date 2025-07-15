//
// Created by kj16609 on 3/22/25.
//

#include "api/IDHANSearchAPI.hpp"
#include "api/helpers/getArrayParameters.hpp"
#include "db/TagSearch.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > IDHANSearchAPI::search( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	// Drogon does not support tag_id=1?tag_id=2 for some reason, But it's possible to be sent like that, So we'll handle it here.
	const auto tag_ids { parseArrayParmeters< std::size_t >( request, "tag_id" ) };
	const auto domain_ids { parseArrayParmeters< TagDomainID >( request, "tag_domains" ) };

	const bool use_stored { request->getOptionalParameter< bool >( "use_stored" ).value_or( false ) };

	std::string query { "" };
	std::string query_template { "" };

	if ( use_stored )
	{
		query_template = "SELECT record_id FROM tag_mappings WHERE tag_id = {0} AND domain_id = {1}";
	}
	else
	{
		query_template =
			"WITH expanded_tags AS ( SELECT UNNEST(expandSearchTags({0}, {1}::smallint)) AS tag_id) SELECT DISTINCT tm.record_id FROM expanded_tags et JOIN tag_mappings tm ON tm.tag_id = et.tag_id AND tm.domain_id = {1}";
	}

	for ( std::size_t tag_i = 0; tag_i < tag_ids.size(); ++tag_i )
	{
		for ( std::size_t domain_i = 0; domain_i < domain_ids.size(); ++domain_i )
		{
			if ( domain_i > 0 ) query += " UNION ";
			query += std::vformat(
				query_template, std::format_args( std::make_format_args( tag_ids[ tag_i ], domain_ids[ domain_i ] ) ) );
		}
	}

	log::info( "Query: {}", query );

	// const auto result { co_await db->execSqlCoro( query, static_cast< TagID >( tag_ids[ 0 ] ) ),
	// static_cast< TagID >(  ) };
	/*
	if ( result.empty() )
	{
		Json::Value root {};
		root[ "file_ids" ] = Json::arrayValue;
		co_return drogon::HttpResponse::newHttpJsonResponse( root );
	}
	else
	{
		Json::Value file_ids {};

		for ( const auto& row : result )
		{
			const auto id { row[ 0 ].as< std::size_t >() };
			file_ids.append( id );
		}

		Json::Value json {};
		json[ "file_ids" ] = std::move( file_ids );

		co_return drogon::HttpResponse::newHttpJsonResponse( json );
	}
	*/
}

} // namespace idhan::api