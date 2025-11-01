//
// Created by kj16609 on 4/17/25.
//

#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

drogon::Task< Json::Value > getSimilarTags(
	const std::string search_value,
	drogon::orm::DbClientPtr db,
	const std::size_t limit )
{
	LOG_DEBUG << "Searching for tag \"{}\"" << search_value;

	const auto wrapped_search_value { '%' + search_value + '%' };

	constexpr std::size_t max_limit { 32 };

	if ( limit > max_limit )
	{
		log::warn( "Tag search came in with absurdly high limit (was {}, clamped to {})", limit, max_limit );
	}

	const auto result { co_await db->execSqlCoro(
		R"(
		SELECT *,
				similarity(tag_text, $2)								AS similarity,
				tag_text = $2											AS exact,
				similarity(tag_text, $2) * coalesce(display_count, 1)	AS score,
				COALESCE(tc.display_count, 0)							AS count
		FROM tags_combined
		         LEFT JOIN total_tag_counts tc USING (tag_id)
		WHERE tag_text LIKE $1
		ORDER BY exact DESC, score DESC, similarity DESC
		limit $3)",
		wrapped_search_value,
		search_value,
		std::min( limit, max_limit ) ) };

	Json::Value tags { Json::arrayValue };

	for ( const auto& row : result )
	{
		Json::Value tag;

		tag[ "value" ] = row[ "tag_text" ].as< std::string >();
		tag[ "tag_text" ] = row[ "tag_text" ].as< std::string >();

		tag[ "similarity" ] = row[ "similarity" ].as< double >();
		tag[ "tag_id" ] = row[ "tag_id" ].as< TagID >();
		tag[ "count" ] = row[ "count" ].as< std::size_t >();

		tags.append( std::move( tag ) );
	}

	co_return tags;
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::autocomplete(
	const drogon::HttpRequestPtr request,
	const std::string search_value )
{
	const auto display_type { request->getOptionalParameter< std::string >( "tag_display_type" ) };

	const std::string display_type_str { display_type.value_or( "storage" ) };
	if ( display_type_str != "storage" && display_type_str != "display" )
	{
		co_return createBadRequest( "Invalid tag display type" );
	}

	// pre-prep the search_value for searching in the database
	const auto db { drogon::app().getDbClient() };

	const std::size_t limit { request->getOptionalParameter< std::size_t >( "limit" ).value_or( 10 ) };

	const auto result { getSimilarTags( search_value, db, limit ) };

	co_return drogon::HttpResponse::newHttpJsonResponse( co_await result );
}

} // namespace idhan::api
