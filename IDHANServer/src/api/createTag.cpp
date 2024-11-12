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

inline static std::recursive_mutex namespace_mtx {};
inline static std::unordered_map< std::string, NamespaceID > namespace_cache {};

drogon::Task< std::optional< NamespaceID > > searchNamespace( const std::string& str, drogon::orm::DbClientPtr db )
{
	const auto result {
		co_await db->execSqlCoro( "SELECT namespace_id FROM tag_namespaces WHERE namespace_text = $1", str )
	};

	if ( result.size() == 0 ) co_return std::nullopt;

	co_return result[ 0 ][ 0 ].as< NamespaceID >();
}

drogon::Task< NamespaceID > findOrCreateNamespace( const std::string& str, drogon::orm::DbClientPtr db )
{
	bool success { false };

	do {
		const auto id_search { co_await searchNamespace( str, db ) };
		if ( id_search.has_value() )
		{
			co_return id_search.value();
		}

		const auto id_creation { co_await db->execSqlCoro(
			"INSERT INTO tag_namespaces (namespace_text) VALUES ($1) ON CONFLICT DO NOTHING RETURNING namespace_id",
			str ) };

		if ( id_creation.size() > 0 )
		{
			success = true;
			co_return id_creation[ 0 ][ 0 ].as< NamespaceID >();
		}
	}
	while ( success != true );
	std::unreachable();
}

drogon::Task< NamespaceID > getNamespaceID( const std::string& namespace_text, drogon::orm::DbClientPtr db )
{
	const NamespaceID id { co_await findOrCreateNamespace( namespace_text, db ) };
	co_return id;
}

drogon::Task< NamespaceID > getNamespaceID( Json::Value namespace_c, drogon::orm::DbClientPtr db )
{
	if ( namespace_c.isString() )
	{
		co_return co_await getNamespaceID( namespace_c.asString(), db );
	}
	else if ( namespace_c.isInt() )
	{
		co_return namespace_c.as< NamespaceID >();
	}
	throw std::runtime_error( "create tag failed: namespace input was neither string or integer" );
}

drogon::Task< std::optional< SubtagID > > searchSubtag( const std::string& str, drogon::orm::DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro( "SELECT subtag_id FROM tag_subtags WHERE subtag_text = $1", str ) };

	if ( result.size() == 0 ) co_return std::nullopt;

	co_return result[ 0 ][ 0 ].as< SubtagID >();
}

drogon::Task< SubtagID > findOrCreateSubtag( const std::string& str, drogon::orm::DbClientPtr db )
{
	bool success { false };

	do {
		const auto id_search { co_await searchSubtag( str, db ) };
		if ( id_search.has_value() )
		{
			co_return id_search.value();
		}

		const auto id_creation { co_await db->execSqlCoro(
			"INSERT INTO tag_subtags (subtag_text) VALUES ($1) ON CONFLICT DO NOTHING RETURNING subtag_id", str ) };

		if ( id_creation.size() > 0 )
		{
			success = true;
			co_return id_creation[ 0 ][ 0 ].as< SubtagID >();
		}
	}
	while ( success != true );
	std::unreachable();
}

drogon::Task< SubtagID > getSubtagID( Json::Value subtag_c, drogon::orm::DbClientPtr db )
{
	if ( subtag_c.isString() )
	{
		co_return co_await findOrCreateSubtag( subtag_c.asString(), db );
	}
	else if ( subtag_c.isInt() )
	{
		co_return subtag_c.as< SubtagID >();
	}
	throw std::runtime_error( "create tag failed: subtag input was neither string or integer" );
}

drogon::Task< std::optional< TagID > >
	searchTagID( const NamespaceID namespace_id, const SubtagID subtag_id, drogon::orm::DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro(
		"SELECT tag_id FROM tags WHERE namespace_id = $1 AND subtag_id = $2", namespace_id, subtag_id ) };

	if ( result.size() == 0 )
	{
		co_return std::nullopt;
	}

	co_return result[ 0 ][ 0 ].as< TagID >();
}

drogon::Task< TagID >
	createTagID( const NamespaceID namespace_id, const SubtagID subtag_id, drogon::orm::DbClientPtr db )
{
	bool success { false };

	do {
		const auto id_search { co_await searchTagID( namespace_id, subtag_id, db ) };
		if ( id_search.has_value() )
		{
			co_return id_search.value();
		}

		const auto id_creation { co_await db->execSqlCoro(
			"INSERT INTO tags (namespace_id, subtag_id) VALUES ($1, $2) RETURNING tag_id", namespace_id, subtag_id ) };

		if ( id_creation.size() > 0 )
		{
			success = true;
			co_return id_creation[ 0 ][ 0 ].as< SubtagID >();
		}
	}
	while ( success != true );
	std::unreachable();
}

drogon::Task< drogon::HttpResponsePtr > IDHANApi::createTagRouter( drogon::HttpRequestPtr request )
{
	const auto json_obj { request->getJsonObject() };

	if ( json_obj == nullptr ) throw std::runtime_error( "create tag router failed. json was null" );

	if ( json_obj->isArray() )
	{
		co_return co_await createBatchedTag( request );
	}

	if ( json_obj->isObject() )
	{
		co_return co_await createSingleTag( request );
	}
}

drogon::Task< drogon::HttpResponsePtr > IDHANApi::createBatchedTag( drogon::HttpRequestPtr request )
{
	std::vector< std::pair< std::future< NamespaceID >, std::future< SubtagID > > > futures {};

	if ( request == nullptr )
	{
		log::error( "/tag/create: null request" );
		throw std::runtime_error( "Null request" );
	}

	// we should have a body
	const auto input_json { request->jsonObject() };

	if ( input_json == nullptr )
	{
		log::error( "/tag/create: no json data" );
		throw std::runtime_error( "No json data" );
	}

	const auto json_array { *input_json };

	Json::Value out {};

	int array_idx { 0 };

	for ( const Json::Value& value : json_array )
	{
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

		auto transaction { db->newTransactionCoro() };

		const NamespaceID namespace_id { co_await getNamespaceID( namespace_c, db ) };
		const SubtagID subtag_id { co_await getSubtagID( subtag_c, db ) };
		const TagID tag_id { co_await createTagID( namespace_id, subtag_id, db ) };

		out[ array_idx ][ "namespace" ] = namespace_id;
		out[ array_idx ][ "subtag" ] = subtag_id;
		out[ array_idx ][ "tag_id" ] = tag_id;
		array_idx += 1;
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

drogon::Task< drogon::HttpResponsePtr > IDHANApi::createSingleTag( drogon::HttpRequestPtr request )
{
	if ( request == nullptr )
	{
		log::error( "/tag/create: null request" );
		throw std::runtime_error( "Null request" );
	}

	// we should have a body
	const auto input_json { request->jsonObject() };

	if ( input_json == nullptr )
	{
		log::error( "/tag/create: no json data" );
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

	const NamespaceID namespace_id { co_await getNamespaceID( namespace_c, db ) };
	const SubtagID subtag_id { co_await getSubtagID( subtag_c, db ) };
	const TagID tag_id { co_await createTagID( namespace_id, subtag_id, db ) };

	Json::Value json {};

	json[ "namespace" ] = namespace_id;
	json[ "subtag" ] = subtag_id;
	json[ "tag_id" ] = tag_id;

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api