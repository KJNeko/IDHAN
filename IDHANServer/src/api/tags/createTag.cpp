//
// Created by kj16609 on 11/9/24.
//

#include <expected>
#include <functional>
#include <ranges>

namespace std
{
template <>
struct hash< std::pair< std::string, std::string > >
{
	std::size_t operator()( const std::pair< std::string, std::string >& p ) const noexcept
	{
		return std::hash< std::string > {}( p.first + ":" + p.second );
	}
};
} // namespace std

#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/drogonArrayBind.hpp"
#include "api/helpers/tags/namespaces.hpp"
#include "api/helpers/tags/subtags.hpp"
#include "api/helpers/tags/tags.hpp"
#include "fgl/defines.hpp"
#include "logging/ScopedTimer.hpp"
#include "logging/log.hpp"
#include "splitTag.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::createTagRouter( const drogon::HttpRequestPtr request )
{
	const auto json_obj { request->getJsonObject() };

	if ( json_obj == nullptr ) throw std::runtime_error( "create tag router failed. json was null" );

	if ( json_obj->isArray() )
	{
		log::debug( "Tag router creating multiple tags" );
		co_return co_await createBatchedTag( request );
	}

	if ( json_obj->isObject() )
	{
		co_return co_await createSingleTag( request );
	}

	co_return createInternalError( "Unable to determine json type for tag router" );
}

drogon::Task< std::expected< std::vector< TagID >, drogon::HttpResponsePtr > > createTags(
	const std::vector< std::pair< std::string, std::string > >& tag_pairs, const drogon::orm::DbClientPtr db )
{
	logging::ScopedTimer timer { "createTags" };

	std::vector< TagID > tag_ids {};

	std::unordered_map< std::pair< std::string, std::string >, TagID > tag_map {};

	std::vector< std::string > namespace_params {};
	std::vector< std::string > subtag_params {};

	for ( const auto& [ namespace_text, subtag_text ] : tag_pairs )
	{
		namespace_params.emplace_back( namespace_text );
		subtag_params.emplace_back( subtag_text );
	}

	try
	{
		const auto result { co_await db->execSqlCoro(
			"SELECT * FROM createBatchTags($1::TEXT[], $2::TEXT[])",
			std::move( namespace_params ),
			std::move( subtag_params ) ) };

		for ( const auto& row : result )
		{
			const auto& tag_id { row[ "tag_id" ].as< TagID >() };
			const auto& namespace_text { row[ "namespace_text" ].as< std::string >() };
			const auto& subtag_text { row[ "subtag_text" ].as< std::string >() };

			tag_map.emplace( std::make_pair( namespace_text, subtag_text ), tag_id );
		}
	}
	catch ( std::exception& e )
	{
		co_return std::unexpected( createInternalError( "Failed to create tags: {}", e.what() ) );
	}

	for ( const auto& [ namespace_text, subtag_text ] : tag_pairs )
	{
		const auto cache_itter = tag_map.find( { namespace_text, subtag_text } );
		if ( cache_itter != tag_map.end() )
		{
			tag_ids.emplace_back( cache_itter->second );
		}
		else
		{
			const auto search_result { co_await db->execSqlCoro(
				"SELECT tag_id FROM tags JOIN tag_namespaces USING (namespace_id) JOIN tag_subtags USING (subtag_id) WHERE namespace_text = $1 AND subtag_text = $2",
				namespace_text,
				subtag_text ) };

			if ( search_result.size() > 0 )
				tag_ids.emplace_back( search_result[ 0 ][ 0 ].as< TagID >() );
			else
				co_return std::
					unexpected( createInternalError( "Failed to create tag {}:{}", namespace_text, subtag_text ) );
		}
	}

	co_return tag_ids;
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::createBatchedTag( const drogon::HttpRequestPtr request )
{
	logging::ScopedTimer timer { "createBatchedTag" };
	// we should have a body
	const auto input_json { request->jsonObject() };

	if ( input_json == nullptr )
	{
		co_return createBadRequest( "No json data" );
	}

	const auto json_array { *input_json };

	auto db { drogon::app().getDbClient() };

	Json::Value out {};
	Json::ArrayIndex index { 0 };

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

	std::ranges::reverse( tag_pairs );

	while ( !tag_pairs.empty() )
	{
		constexpr std::size_t chunk_size { 1024 };
		std::vector< std::pair< std::string, std::string > > tag_pairs_chunk {};
		tag_pairs_chunk.reserve( chunk_size );

		for ( std::size_t i = 0; i < chunk_size && !tag_pairs.empty(); ++i )
		{
			tag_pairs_chunk.push_back( tag_pairs.back() );
			tag_pairs.pop_back();
		}

		try
		{
			const auto result { co_await createTags( tag_pairs_chunk, db ) };

			if ( !result ) co_return result.error();

			for ( const auto& tag_id : result.value() )
			{
				out[ index ][ "tag_id" ] = tag_id;
				index += 1;
			}
		}
		catch ( std::exception& e )
		{
			co_return createInternalError( "Failed to create batched tags: {}", e.what() );
		}
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

drogon::Task< drogon::HttpResponsePtr > TagAPI::createSingleTag( drogon::HttpRequestPtr request )
{
	logging::ScopedTimer timer { "createSingleTime" };
	if ( request == nullptr )
	{
		log::error( "/tags/create: null request" );
		throw std::runtime_error( "Null request" );
	}

	// we should have a body
	const auto input_json { request->jsonObject() };

	if ( input_json == nullptr )
	{
		log::error( "/tags/create: no json data" );
		throw std::runtime_error( "No json data" );
	}

	const auto& value { *input_json };

	auto namespace_c { value[ "namespace" ] };
	auto subtag_c { value[ "subtag" ] };

	if ( auto tag_data = value[ "tag" ]; tag_data.isString() )
	{
		// split the tag
		const std::string str { tag_data.asString() };
		if ( !str.contains( ':' ) )
		{
			subtag_c = value[ "subtag" ].asString();
		}
		else
		{
			const auto [ n, s ] = tags::split( str );
			namespace_c = std::move( n );
			subtag_c = std::move( s );
		}
	}
	else if ( namespace_c.isNull() && subtag_c.isNull() )
	{
		co_return drogon::HttpResponse::newNotFoundResponse();
	}

	auto db { drogon::app().getDbClient() };

	const auto namespace_id { co_await findOrCreateNamespace( namespace_c.asString(), db ) };
	const auto subtag_id { co_await findOrCreateSubtag( subtag_c.asString(), db ) };

	if ( !namespace_id ) co_return namespace_id.error();
	if ( !subtag_id ) co_return subtag_id.error();

	log::debug( "Got namespace id {} for {} ", namespace_id.value(), namespace_c.asString() );
	log::debug( "Got subtag id {} for {}", subtag_id.value(), subtag_c.asString() );

	const auto tag_id { co_await findOrCreateTag( namespace_id.value(), subtag_id.value(), db ) };

	if ( !tag_id ) co_return tag_id.error();

	log::debug( "Got tag id {} for tag ({}, {})", tag_id.value(), namespace_id.value(), subtag_id.value() );

	Json::Value json {};

	json[ "namespace" ] = namespace_id.value();
	json[ "subtag" ] = subtag_id.value();
	json[ "tag_id" ] = tag_id.value();

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api