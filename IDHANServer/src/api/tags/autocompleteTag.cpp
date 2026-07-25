//
// Created by kj16609 on 4/17/25.
//

#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "profiling/tracy.hpp"

namespace idhan::api
{

drogon::Task< Json::Value > getSimilarTags(
	const std::string search_value,
	drogon::orm::DbClientPtr db,
	const std::size_t limit,
	const bool include_unused )
{
	ZoneScopedN( "getSimilarTags" );
	log::debug( "Searching for tag \"{}\"", search_value );

	const bool is_negative { search_value.starts_with( '-' ) };
	const std::string real_search_value { is_negative ? search_value.substr( 1 ) : search_value };
	const auto wrapped_search_value { format_ns::format( "%{}%", real_search_value ) };

	// A serious tag editor shows many suggestions at once (Hydrus routinely 100+); the previous cap
	// of 32 was too low. The trigram GIN index makes the cap cheap — it bounds the sort, not the scan.
	constexpr std::size_t max_limit { 256 };

	if ( limit > max_limit )
	{
		// Not a warning: a UI legitimately requests large pages. Only truly extreme values matter.
		log::debug( "Tag search limit {} exceeds the cap; clamped to {}", limit, max_limit );
	}

	const auto only_used_query { R"(
		SELECT	tag_text												AS tag_text,
				tag_id													AS tag_id,
				similarity(tag_text, $2)								AS similarity,
				tag_text = $2											AS exact,
				similarity(tag_text, $2) * max(tc.display_count)		AS score,
				max(tc.display_count)									AS display_count,
				max(tc.storage_count)									AS storage_count
		FROM tags
		         LEFT JOIN tag_counts tc USING (tag_id)
		WHERE tag_text LIKE $1 AND COALESCE(tc.display_count, 0) > 0
		GROUP BY tags.tag_id
		ORDER BY exact DESC, score DESC, similarity DESC
		limit $3
		)" };

	const auto all_query { R"(
		SELECT	tag_text												AS tag_text,
				tag_id													AS tag_id,
				similarity(tag_text, $2)								AS similarity,
				tag_text = $2											AS exact,
				similarity(tag_text, $2) * max(tc.display_count)		AS score,
				max(tc.display_count)									AS display_count,
				max(tc.storage_count)									AS storage_count
		FROM tags
		         LEFT JOIN tag_counts tc USING (tag_id)
		WHERE tag_text LIKE $1
		GROUP BY tags.tag_id
		-- score is NULL for unused tags; plain DESC is NULLS FIRST and would rank them above every used tag
		ORDER BY exact DESC, score DESC NULLS LAST, similarity DESC
		limit $3
		)" };

	log::debug( include_unused ? "Using all tags query" : "Using only used tags query" );

	const auto result { co_await db->execSqlCoro(
		include_unused ? all_query : only_used_query,
		wrapped_search_value,
		real_search_value,
		std::min( limit, max_limit ) ) };

	Json::Value tags { Json::arrayValue };

	log::debug( "Got {} results for autocomplete", result.size() );

	for ( const auto& row : result )
	{
		Json::Value tag {};

		const auto tag_text { row[ "tag_text" ].as< std::string >() };

		tag[ "value" ] = tag_text;
		tag[ "tag_text" ] = tag_text;

		tag[ "similarity" ] = row[ "similarity" ].as< double >();
		tag[ "tag_id" ] = row[ "tag_id" ].as< TagID >();
		tag[ "count" ] = row[ "display_count" ].as< std::size_t >();

		log::debug( "Tag \'{}\' had similarity of {}", tag_text, row[ "similarity" ].as< double >() );

		tags.append( std::move( tag ) );
	}

	co_return tags;
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::autocomplete(
	const drogon::HttpRequestPtr request,
	const std::string search_value )
{
	ZoneScopedN( "TagAPI::autocomplete" );
	const auto display_type { request->getOptionalParameter< std::string >( "tag_display_type" ) };

	const std::string display_type_str { display_type.value_or( "storage" ) };
	if ( display_type_str != "storage" && display_type_str != "display" )
	{
		co_return createBadRequest( "Invalid tag display type" );
	}

	// pre-prep the search_value for searching in the database
	const auto db { drogon::app().getDbClient() };

	const std::size_t limit { request->getOptionalParameter< std::size_t >( "limit" ).value_or( 10 ) };
	const bool include_unused { request->getOptionalParameter< bool >( "include_unused" ).value_or( true ) };

	const auto result { co_await getSimilarTags( search_value, db, limit, include_unused ) };

	log::debug( "Autocomplete response: {}", result.toStyledString() );

	co_return drogon::HttpResponse::newHttpJsonResponse( result );
}

} // namespace idhan::api
