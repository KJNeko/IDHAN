//
// Created by kj16609 on 4/17/25.
//

#include "api/IDHANTagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

drogon::Task< Json::Value >
	getSimilarTags( const std::string search_value, drogon::orm::DbClientPtr db, const std::size_t limit )
{
	LOG_DEBUG << "Searching for tag \"{}\"" << search_value;

	const auto result { co_await db->execSqlCoro(
		"SELECT tag_id, tag_text, similarity($1, tag_text) AS similarity FROM tags_combined WHERE tag_text LIKE $1 ORDER BY similarity DESC LIMIT $2",
		search_value,
		limit ) };

	Json::Value root;

	if ( result.size() == 0 )
	{
		root[ "tags" ] = Json::Value( Json::arrayValue );
		co_return root;
	}

	for ( const auto& row : result )
	{
		Json::Value tag;

		//TODO: Hydrus only, Hide behind a toggle for hydrus support
		tag[ "value" ] = row[ "tag_text" ].as< std::string >();
		tag[ "tag_text" ] = row[ "tag_text" ].as< std::string >();

		tag[ "similarity" ] = row[ "similarity" ].as< double >();
		tag[ "tag_id" ] = row[ "tag_id" ].as< TagID >();
		tag[ "count" ] = 0;

		root[ "tags" ].append( std::move( tag ) );
	}

	co_return root;
}

drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::
	autocomplete( drogon::HttpRequestPtr request, std::string search_value )
{
	const auto display_type { request->getOptionalParameter< std::string >( "tag_display_type" ) };

	const std::string display_type_str { display_type.value_or( "storage" ) };
	if ( display_type_str != "storage" && display_type_str != "display" )
	{
		co_return createBadRequest( "Invalid tag display type" );
	}

	// pre-prep the search_value for searching in the database
	search_value = '%' + search_value + '%';
	const auto db { drogon::app().getDbClient() };

	const std::size_t limit { request->getOptionalParameter< std::size_t >( "limit" ).value_or( 10 ) };

	const auto result { getSimilarTags( search_value, db, limit ) };

	co_return drogon::HttpResponse::newHttpJsonResponse( co_await result );
}

} // namespace idhan::api
