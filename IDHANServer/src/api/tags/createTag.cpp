//
// Created by kj16609 on 11/9/24.
//

#include <expected>
#include <ranges>

#include "api/IDHANTagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/tags/namespaces.hpp"
#include "api/helpers/tags/subtags.hpp"
#include "fgl/defines.hpp"
#include "logging/ScopedTimer.hpp"
#include "logging/log.hpp"
#include "splitTag.hpp"

namespace idhan::api
{

drogon::Task< std::expected< NamespaceID, drogon::HttpResponsePtr > >
	getNamespaceID( const Json::Value& namespace_c, drogon::orm::DbClientPtr db )
{
	if ( namespace_c.isString() )
	{
		const std::string& str { namespace_c.asString() };

		co_return co_await findOrCreateNamespace( str, db );
	}
	else if ( namespace_c.isInt() )
	{
		co_return namespace_c.as< NamespaceID >();
	}
	throw std::runtime_error( "create tag failed: namespace input was neither string or integer" );
}

drogon::Task< std::expected< SubtagID, drogon::HttpResponsePtr > >
	getSubtagID( const Json::Value subtag_c, const drogon::orm::DbClientPtr db )
{
	if ( subtag_c.isString() )
	{
		co_return co_await findOrCreateSubtag( subtag_c.asString(), db );
	}
	else if ( subtag_c.isInt() )
	{
		co_return subtag_c.as< SubtagID >();
	}

	co_return std::unexpected( createBadRequest( "Failed to get subtag id: Json was not string or integer" ) );
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

drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > >
	createTagID( const NamespaceID namespace_id, const SubtagID subtag_id, drogon::orm::DbClientPtr db )
{
	const auto id_search { co_await searchTagID( namespace_id, subtag_id, db ) };
	if ( id_search.has_value() )
	{
		co_return id_search.value();
	}

	const auto id_creation { co_await db->execSqlCoro(
		"INSERT INTO tags (namespace_id, subtag_id) VALUES ($1, $2) RETURNING tag_id", namespace_id, subtag_id ) };

	if ( id_creation.size() > 0 )
	{
		co_return id_creation[ 0 ][ 0 ].as< SubtagID >();
	}

	co_return std::unexpected( createInternalError( "Unable to create tag with {}:{}", namespace_id, subtag_id ) );
}

drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::createTagRouter( drogon::HttpRequestPtr request )
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

std::string pgEscape( const std::string& str )
{
	std::string cleaned {};
	cleaned.reserve( str.size() * 2 );

	if ( str.empty() ) return "\"\"";
	if ( str == "null" ) return "\"null\"";

	bool contains_comma { false };

	for ( const auto& c : str )
	{
		switch ( c )
		{
			case '}':
				[[fallthrough]];
			case '{':
				[[fallthrough]];
			case '\"':
				[[fallthrough]];
			case '\\':
				[[fallthrough]];
				cleaned.push_back( '\\' );
			case ',':
				[[fallthrough]];
				contains_comma = contains_comma || ( c == ',' );
			default:
				cleaned.push_back( c );
		}
	}

	if ( contains_comma ) return format_ns::format( "\"{}\"", cleaned );
	return cleaned;
}

drogon::Task< std::expected< std::vector< TagID >, drogon::HttpResponsePtr > >
	createTags( const std::vector< std::pair< std::string, std::string > >& tag_pairs, drogon::orm::DbClientPtr db )
{
	logging::ScopedTimer timer { "createTags" };
	std::string namespaces { "{" };
	std::string subtags { "{" };

	namespaces.reserve( tag_pairs.size() * 64 );
	subtags.reserve( tag_pairs.size() * 64 );

	std::size_t counter { 0 };

	for ( const auto& [ namespace_txt, subtag_txt ] : tag_pairs )
	{
		// replace any single ' with double '
		namespaces += pgEscape( namespace_txt );
		subtags += pgEscape( subtag_txt );

		if ( counter < tag_pairs.size() - 1 )
		{
			namespaces += ",";
			subtags += ",";
		}

		++counter;
	}

	namespaces += "}";
	subtags += "}";

	std::vector< TagID > tag_ids {};

	try
	{
		const auto result { co_await db->execSqlCoro( "SELECT createBatchedTag($1, $2)", namespaces, subtags ) };

		if ( result.size() != tag_pairs.size() )
			co_return std::unexpected( createInternalError(
				"Expected number of tag returns does not match {} == {}", result.size(), tag_pairs.size() ) );

		tag_ids.reserve( result.size() );

		for ( const auto& tag : result ) tag_ids.emplace_back( tag[ 0 ].as< TagID >() );
	}
	catch ( std::exception& e )
	{
		co_return std::unexpected( createInternalError(
			"Failed to create tags using createBatchedTags(): {}\nNamespaces: {}\nSubtags: {}",
			e.what(),
			namespaces,
			subtags ) );
	}

	co_return tag_ids;
}

drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::createBatchedTag( drogon::HttpRequestPtr request )
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

	try
	{
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

		Json::Value out {};
		Json::ArrayIndex index { 0 };

		const auto result { co_await createTags( tag_pairs, db ) };

		if ( !result.has_value() ) co_return result.error();

		for ( const auto& tag_id : result.value() )
		{
			out[ index ][ "tag_id" ] = tag_id;
			index += 1;
		}

		co_return drogon::HttpResponse::newHttpJsonResponse( out );
	}
	catch ( std::exception& e )
	{
		co_return createInternalError( "Failed to create batched tags: {}", e.what() );
	}
}

drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::createSingleTag( drogon::HttpRequestPtr request )
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

	if ( !namespace_id.has_value() ) co_return namespace_id.error();
	if ( !subtag_id.has_value() ) co_return subtag_id.error();

	log::debug( "Got namespace id {} for {} ", namespace_id.value(), namespace_c.asString() );
	log::debug( "Got subtag id {} for {}", subtag_id.value(), subtag_c.asString() );

	const auto tag_id { co_await createTagID( namespace_id.value(), subtag_id.value(), db ) };

	if ( !tag_id.has_value() ) co_return tag_id.error();

	log::debug( "Got tag id {} for tag ({}, {})", tag_id.value(), namespace_id.value(), subtag_id.value() );

	Json::Value json {};

	json[ "namespace" ] = namespace_id.value();
	json[ "subtag" ] = subtag_id.value();
	json[ "tag_id" ] = tag_id.value();

	co_return drogon::HttpResponse::newHttpJsonResponse( json );
}

} // namespace idhan::api