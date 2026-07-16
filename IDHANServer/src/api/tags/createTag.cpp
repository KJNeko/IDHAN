//
// Created by kj16609 on 11/9/24.
//

#include <expected>

#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"
#include "fgl/defines.hpp"
#include "logging/ScopedTimer.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::api
{

drogon::Task< std::expected< std::vector< TagID >, drogon::HttpResponsePtr > > createTagsFromPairs(
	const std::vector< std::pair< std::string, std::string > >& tag_pairs,
	const DbClientPtr db )
{
	logging::ScopedTimer timer { "createTags" };

	std::vector< TagID > tag_ids {};
	tag_ids.reserve( tag_pairs.size() );

	std::vector< std::string > namespace_params {};
	namespace_params.reserve( tag_pairs.size() );
	std::vector< std::string > subtag_params {};
	subtag_params.reserve( tag_pairs.size() );

	for ( const auto& [ namespace_text, subtag_text ] : tag_pairs )
	{
		namespace_params.emplace_back( namespace_text );
		subtag_params.emplace_back( subtag_text );
	}

	if ( tag_pairs.empty() )
	{
		co_return std::unexpected( createBadRequest( "No tags to create" ) );
	}

	try
	{
		const auto result { co_await db->execSqlCoro(
			"SELECT tag_id FROM createBatchTags($1::TEXT[], $2::TEXT[])",
			std::move( namespace_params ),
			std::move( subtag_params ) ) };

		for ( const auto& row : result )
		{
			const auto& tag_id { row[ "tag_id" ].as< TagID >() };

			if ( !( tag_id > 0 ) ) [[unlikely]]
				co_return std::unexpected(
					createInternalError( "Failed to create tag, got {}. Expected tag_id > 0", tag_id ) );

			tag_ids.emplace_back( tag_id );
		}

		if ( tag_ids.size() != tag_pairs.size() )
		{
			co_return std::unexpected( createInternalError(
				"Failed to create tags. Count mismatch Expected {} got {} ", tag_pairs.size(), tag_ids.size() ) );
		}

		co_return tag_ids;
	}
	catch ( std::exception& e )
	{
		co_return std::unexpected( createInternalError( "Failed to create tags: {}", e.what() ) );
	}
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::createTagsFromRequest( const drogon::HttpRequestPtr request )
{
	logging::ScopedTimer timer { "createBatchedTag" };
	const auto input_json { request->jsonObject() };

	if ( input_json == nullptr )
	{
		co_return createBadRequest( "No json data" );
	}

	const auto& json_array { *input_json };

	// iterating a non-array root visits an object's values, and operator[] on
	// a non-object value throws Json::LogicError, which would surface as a 500
	if ( !json_array.isArray() ) co_return createBadRequest( "Invalid json object. Expected array as root item" );

	auto db { drogon::app().getDbClient() };

	std::vector< std::pair< std::string, std::string > > tag_pairs {};
	tag_pairs.reserve( json_array.size() );
	for ( const auto& json_array_item : json_array )
	{
		const auto& namespace_j { json_array_item[ "namespace" ] };
		const auto& subtag_j { json_array_item[ "subtag" ] };

		if ( !namespace_j.isString() ) co_return createBadRequest( "Invalid namespace: Expected string" );
		if ( !subtag_j.isString() ) co_return createBadRequest( "Invalid subtag: Expected string" );

		tag_pairs.emplace_back( namespace_j.asString(), subtag_j.asString() );
	}

	const auto tag_ids { co_await createTagsFromPairs( tag_pairs, db ) };
	if ( !tag_ids ) co_return tag_ids.error();

	for ( const auto& tag_id : tag_ids.value() ) FGL_ASSERT( tag_id > 0, "TagID was not valid" );

	Json::Value out {};
	Json::ArrayIndex index { 0 };
	for ( const auto& tag_id : tag_ids.value() )
	{
		Json::Value tag_json {};
		tag_json[ "tag_id" ] = tag_id;
		out[ index++ ] = tag_json;
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

} // namespace idhan::api
