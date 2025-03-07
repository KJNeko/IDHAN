//
// Created by kj16609 on 11/9/24.
//

#include <pqxx/transaction_base>

#include <ranges>

#include "api/IDHANTagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "splitTag.hpp"

namespace idhan::api
{

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

struct NamespaceCache
{
	NamespaceID m_empty_namespace { 0 };

	std::unordered_map< std::string, NamespaceID > m_id {};

	NamespaceID find( const std::string& str )
	{
		if ( str.empty() && m_empty_namespace != 0 ) return m_empty_namespace;

		if ( const auto itter = m_id.find( str ); itter != m_id.end() )
		{
			return itter->second;
		}
		else
		{
			//TODO: INVALID_NAMESPACE_ID
			return 0;
		}
	}

	void insert( const std::string& namespace_text, NamespaceID id )
	{
		if ( namespace_text.empty() ) [[unlikely]]
			m_empty_namespace = id;
		else [[likely]]
			m_id.emplace( namespace_text, id );
	}
};

inline thread_local static std::unique_ptr< NamespaceCache > namespace_cache { std::make_unique< NamespaceCache >() };

drogon::Task< NamespaceID > getNamespaceID( const Json::Value& namespace_c, drogon::orm::DbClientPtr db )
{
	if ( namespace_c.isString() )
	{
		const std::string& str { namespace_c.asString() };

		auto possible_id { namespace_cache->find( str ) };

		if ( possible_id == 0 )
		{
			possible_id = co_await getNamespaceID( str, db );
			namespace_cache->insert( str, possible_id );
		}

		co_return possible_id;
	}
	else if ( namespace_c.isInt() )
	{
		co_return namespace_c.as< NamespaceID >();
	}
	throw std::runtime_error( "create tag failed: namespace input was neither string or integer" );
}

drogon::Task< std::optional< SubtagID > > searchSubtag( const std::string& str, const drogon::orm::DbClientPtr db )
{
	const auto result { co_await db->execSqlCoro( "SELECT subtag_id FROM tag_subtags WHERE subtag_text = $1", str ) };

	if ( result.size() == 0 ) co_return std::nullopt;

	co_return result[ 0 ][ 0 ].as< SubtagID >();
}

drogon::Task< SubtagID > findOrCreateSubtag( const std::string& str, const drogon::orm::DbClientPtr db )
{
	const auto id_search { co_await searchSubtag( str, db ) };
	if ( id_search.has_value() )
	{
		co_return id_search.value();
	}

	const auto id_creation { co_await db->execSqlCoro(
		"INSERT INTO tag_subtags (subtag_text) VALUES ($1) ON CONFLICT DO NOTHING RETURNING subtag_id", str ) };

	if ( id_creation.size() > 0 )
	{
		co_return id_creation[ 0 ][ 0 ].as< SubtagID >();
	}
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
	const auto id_search { co_await searchTagID( namespace_id, subtag_id, db ) };
	if ( id_search.has_value() )
	{
		co_return id_search.value();
	}

	const auto unique_check { co_await db->execSqlCoro(
		"SELECT tag_id FROM tags WHERE namespace_id = $1 AND subtag_id = $2", namespace_id, subtag_id ) };

	if ( !unique_check.empty() )
	{
		createConflict(
			"Tag ID {} already exists with namespace {} and subtag {}",
			unique_check[ 0 ][ 0 ].as< TagID >(),
			namespace_id,
			subtag_id );
	}

	const auto id_creation { co_await db->execSqlCoro(
		"INSERT INTO tags (namespace_id, subtag_id) VALUES ($1, $2) RETURNING tag_id", namespace_id, subtag_id ) };

	if ( id_creation.size() > 0 )
	{
		co_return id_creation[ 0 ][ 0 ].as< SubtagID >();
	}
}

drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::createTagRouter( drogon::HttpRequestPtr request )
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

std::string pgEscape( const std::string& str )
{
	std::string cleaned {};
	cleaned.reserve( str.size() * 2 );

	if ( str.empty() ) return "\"\"";
	if ( str == "null" ) return "\"null\"";

	bool contains_comma { false };

	for ( const auto& c : str )
	{
		if ( c == '}' ) cleaned.push_back( '\\' );
		if ( c == '{' ) cleaned.push_back( '\\' );
		if ( c == '\"' ) cleaned.push_back( '\\' );
		if ( c == '\'' ) cleaned.push_back( '\'' );
		if ( c == '\\' ) cleaned.push_back( '\\' );
		if ( c == ',' ) contains_comma = true;
		cleaned.push_back( c );
	}

	if ( contains_comma ) return std::format( "\"{}\"", cleaned );
	return cleaned;
}

drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::createBatchedTag( drogon::HttpRequestPtr request )
{
	// we should have a body
	const auto input_json { request->jsonObject() };

	if ( input_json == nullptr )
	{
		co_return createBadRequest( "No json data" );
	}

	const auto json_array { *input_json };

	auto db { drogon::app().getDbClient() };

	std::string namespaces { "{" };
	std::string subtags { "{" };

	namespaces.reserve( json_array.size() );
	subtags.reserve( json_array.size() );

	std::size_t counter { 0 };

	for ( const auto& value : json_array )
	{
		auto namespace_c { value[ "namespace" ] };
		auto subtag_c { value[ "subtag" ] };

		std::string namespace_txt { namespace_c.asString() };
		std::string subtag_txt { subtag_c.asString() };

		// replace any single ' with double '
		namespaces += pgEscape( namespace_txt );
		subtags += pgEscape( subtag_txt );

		if ( counter < json_array.size() - 1 )
		{
			namespaces += ",";
			subtags += ",";
		}

		++counter;
	}

	namespaces += "}";
	subtags += "}";

	try
	{
		const auto result { co_await db->execSqlCoro( "SELECT createBatchedTag($1, $2)", namespaces, subtags ) };

		FGL_ASSERT(
			result.size() == json_array.size(),
			std::format( "Expected number of tag returns does not match {} == {}", result.size(), json_array.size() ) );

		Json::Value out {};

		Json::ArrayIndex index { 0 };
		for ( const auto& row : result )
		{
			out[ index ][ "tag_id" ] = row[ 0 ].as< TagID >();
			index += 1;
		}

		co_return drogon::HttpResponse::newHttpJsonResponse( out );
	}
	catch ( ... )
	{
		log::error( "Failed to create batched tags: \n{}, \n{}", namespaces, subtags );
		std::rethrow_exception( std::current_exception() );
	}
}

drogon::Task< drogon::HttpResponsePtr > IDHANTagAPI::createSingleTag( drogon::HttpRequestPtr request )
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