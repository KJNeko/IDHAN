//
// Created by kj16609 on 11/9/24.
//

#include <oneapi/tbb/detail/_range_common.h>

#include <ranges>

#include "IDHANApi.hpp"
#include "core/tags.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

void IDHANApi::tagInfo( const drogon::HttpRequestPtr& request, ResponseFunction&& callback, TagID tag_id )
{
	log::info( "/tag/{}/info", tag_id );
}

std::variant< NamespaceID, std::string > getNamespaceComponent( const Json::Value& value )
{}

drogon::Task< drogon::HttpResponsePtr > IDHANApi::createTag( drogon::HttpRequestPtr request )
{
	log::debug( "/tag/create" );

	if ( request == nullptr )
	{
		log::error( "/tag/create: null request" );
	}

	// we should have a body
	const auto input_json { request->jsonObject() };

	if ( input_json == nullptr )
	{
		log::error( "/tag/create: no json data" );
	}

	const auto& value { *input_json };

	const auto namespace_c { value[ "namespace" ] };
	const auto subtag_c { value[ "subtag" ] };

	NamespaceID namespace_id { std::numeric_limits< NamespaceID >::quiet_NaN() };
	SubtagID subtag_id { std::numeric_limits< SubtagID >::quiet_NaN() };

	auto db { drogon::app().getDbClient() };

	if ( namespace_c.isString() )
	{
		const auto namespace_text { namespace_c.asString() };
		const auto search_result {
			co_await db
				->execSqlCoro( "SELECT namespace_id FROM tag_namespaces WHERE namespace_text = $1", namespace_text )
		};

		if ( search_result.size() > 0 )
		{
			log::debug( "createTag: Found namespace_id: {}", namespace_id );
			namespace_id = search_result[ 0 ][ 0 ].as< NamespaceID >();
		}
		else
		{
			const auto create_result { co_await db->execSqlCoro(
				"INSERT INTO tag_namespaces (namespace_text) VALUES ($1) ON CONFLICT (namespace_text) DO UPDATE SET namespace_text = EXCLUDED.namespace_text RETURNING namespace_id",
				namespace_text ) };

			if ( create_result.size() == 0 ) throw std::runtime_error( "create tag failed" );

			log::debug( "createTag: Created namespace: {}:{}", namespace_id, namespace_text );

			namespace_id = create_result[ 0 ][ 0 ].as< NamespaceID >();
		}
	}
	else if ( namespace_c.isInt() )
	{
		namespace_id = namespace_c.as< NamespaceID >();
	}
	else
		throw std::runtime_error( "create tag failed: namespace input was neither string or integer" );

	if ( subtag_c.isString() )
	{
		const auto subtag_text { subtag_c.asString() };
		log::debug( "Subtag input was string : {}", subtag_text );
		const auto search_result {
			co_await db->execSqlCoro( "SELECT subtag_id FROM tag_subtags WHERE subtag_text = $1", subtag_text )
		};

		if ( search_result.size() > 0 )
		{
			subtag_id = search_result[ 0 ][ 0 ].as< SubtagID >();
		}
		else
		{
			const auto create_result { co_await db->execSqlCoro(
				"INSERT INTO tag_subtags (subtag_text) VALUES ($1) ON CONFLICT (subtag_text) DO UPDATE SET subtag_text = EXCLUDED.subtag_text RETURNING subtag_id",
				subtag_text ) };

			if ( create_result.size() == 0 ) throw std::runtime_error( "create tag failed" );

			log::debug( "createTag: Created subtag: {}:{}", subtag_id, subtag_text );

			subtag_id = create_result[ 0 ][ 0 ].as< SubtagID >();
		}
	}
	else if ( subtag_c.isInt() )
	{
		log::debug( "Subtag input was integer : {}", subtag_c.as< SubtagID >() );
		subtag_id = subtag_c.as< SubtagID >();
	}
	else
		throw std::runtime_error( "create tag failed: subtag input was neither string or integer" );

	// We now have both namespace and subtag ids, So we can try searching for it.
	const auto search_result { co_await db->execSqlCoro(
		"SELECT tag_id FROM tags WHERE namespace_id = $1 AND subtag_id = $2", namespace_id, subtag_id ) };

	if ( std::isnan( namespace_id ) )
	{
		throw std::runtime_error( "create tag failed: namespace was not set" );
	}

	if ( std::isnan( subtag_id ) )
	{
		throw std::runtime_error( "create tag failed: subtag_id is nan" );
	}

	Json::Value json {};

	json[ "namespace" ] = namespace_id;
	json[ "subtag" ] = subtag_id;

	if ( search_result.size() > 0 )
	{
		// The tag existed.
		json[ "tag_id" ] = search_result[ 0 ][ 0 ].as< TagID >();
	}
	else
	{
		const auto create_result { co_await db->execSqlCoro(
			"INSERT INTO tags (namespace_id, subtag_id) VALUES ($1, $2) ON CONFLICT (namespace_id, subtag_id) DO UPDATE SET namespace_id = EXCLUDED.namespace_id RETURNING tag_id",
			namespace_id,
			subtag_id ) };

		json[ "tag_id" ] = create_result[ 0 ][ 0 ].as< TagID >();
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api