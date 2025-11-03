//
// Created by kj16609 on 11/9/24.
//

#include <expected>
#include <functional>
#include <ranges>

#include "api/TagAPI.hpp"
#include "api/helpers/ExpectedTask.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/drogonArrayBind.hpp"
#include "fgl/defines.hpp"
#include "logging/ScopedTimer.hpp"

template <>
struct std::hash< std::pair< std::string, std::string > >
{
	std::size_t operator()( const std::pair< std::string, std::string >& p ) const noexcept
	{
		return std::hash< std::string > {}( p.first + ":" + p.second );
	}
};

template <>
struct std::hash< std::pair< idhan::NamespaceID, idhan::SubtagID > >
{
	std::size_t operator()( const std::pair< idhan::NamespaceID, idhan::SubtagID >& p ) const noexcept
	{
		static_assert(
			( sizeof( idhan::NamespaceID ) == sizeof( std::uint32_t ) )
				&& ( sizeof( idhan::SubtagID ) == sizeof( std::uint32_t ) ),
			"pair hash of NamespaceID and SubtagID only works with 32 bit, If you get this error, you need to write a better hasher" );

		std::size_t hash { 0 };
		hash = p.second;
		hash |= static_cast< std::size_t >( p.first ) << 32;

		return hash;
	}
};

namespace idhan::api
{

drogon::Task< std::unordered_map< std::string, NamespaceID > > getNamespaces(
	std::set< std::string > namespace_set,
	DbClientPtr db )
{
	std::unordered_map< std::string, NamespaceID > map {};

	std::vector< std::string > namespace_texts {};
	std::ranges::copy( namespace_set, std::back_inserter( namespace_texts ) );

	auto namespace_selection { co_await db->execSqlCoro(
		"SELECT namespace_id, namespace_text FROM tag_namespaces WHERE namespace_text = ANY($1::TEXT[])",
		std::forward< std::vector< std::string > >( namespace_texts ) ) };

	if ( namespace_selection.size() != namespace_set.size() )
	{
		// do an insertion
		co_await db->execSqlCoro(
			"INSERT INTO tag_namespaces (namespace_text) VALUES (UNNEST($1::TEXT[])) ON CONFLICT DO NOTHING",
			std::forward< std::vector< std::string > >( namespace_texts ) );

		// select again
		namespace_selection = co_await db->execSqlCoro(
			"SELECT namespace_id, namespace_text FROM tag_namespaces WHERE namespace_text = ANY($1::TEXT[])",
			std::forward< std::vector< std::string > >( namespace_texts ) );
	}

	for ( const auto& row : namespace_selection )
	{
		const auto namespace_id { row[ 0 ].as< NamespaceID >() };
		const auto namespace_text { row[ 1 ].as< std::string >() };
		map.emplace( namespace_text, namespace_id );
	}

	co_return map;
}

drogon::Task< std::unordered_map< std::string, SubtagID > > getSubtags(
	std::set< std::string > subtag_set,
	DbClientPtr db )
{
	std::unordered_map< std::string, SubtagID > map {};

	std::vector< std::string > subtag_texts {};
	std::ranges::copy( subtag_set, std::back_inserter( subtag_texts ) );

	auto subtag_selection { co_await db->execSqlCoro(
		"SELECT subtag_id, subtag_text FROM tag_subtags WHERE subtag_text = ANY($1::TEXT[])",
		std::forward< std::vector< std::string > >( subtag_texts ) ) };

	for ( const auto& row : subtag_selection )
	{
		const auto subtag_id { row[ 0 ].as< SubtagID >() };
		const auto subtag_text { row[ 1 ].as< std::string >() };
		map.emplace( subtag_text, subtag_id );
	}

	std::vector< std::string > unmapped_subtags {};
	for ( const auto& text : subtag_set )
	{
		if ( auto itter = map.find( text ); itter == map.end() ) unmapped_subtags.emplace_back( text );
	}

	if ( !unmapped_subtags.empty() )
	{
		log::debug( "{} of {} subtags were not seen before", unmapped_subtags.size(), subtag_set.size() );
		const auto insert_result { co_await db->execSqlCoro(
			"INSERT INTO tag_subtags (subtag_text) VALUES (UNNEST($1::TEXT[])) ON CONFLICT DO NOTHING RETURNING subtag_id, subtag_text",
			std::forward< std::vector< std::string > >( subtag_texts ) ) };

		for ( const auto& row : insert_result )
		{
			const auto subtag_id { row[ 0 ].as< SubtagID >() };
			const auto subtag_text { row[ 1 ].as< std::string >() };
			map.emplace( subtag_text, subtag_id );
		}
	}

	co_return map;
}

drogon::Task< std::unordered_map< std::pair< NamespaceID, SubtagID >, TagID > > getTags(
	std::vector< NamespaceID > namespace_ids,
	std::vector< SubtagID > subtag_ids,
	DbClientPtr db )
{
	std::unordered_map< std::pair< NamespaceID, SubtagID >, TagID > map {};

	map.reserve( namespace_ids.size() );

	const auto select_result { co_await db->execSqlCoro(
		"WITH t(namespace_id, subtag_id) AS (SELECT * FROM UNNEST($1::" NAMESPACE_ID_PG_TYPE_NAME
		"[], $2::" SUBTAG_ID_PG_TYPE_NAME
		"[])) SELECT tag_id, namespace_id, subtag_id FROM t JOIN tags USING (namespace_id, subtag_id)",
		std::forward< std::vector< NamespaceID > >( namespace_ids ),
		std::forward< std::vector< SubtagID > >( subtag_ids ) ) };

	if ( select_result.size() != namespace_ids.size() )
	{
		const auto new_tag_ids { co_await db->execSqlCoro(
			"INSERT INTO tags (namespace_id, subtag_id) VALUES (UNNEST($1::" NAMESPACE_ID_PG_TYPE_NAME
			"[]), UNNEST($2::" SUBTAG_ID_PG_TYPE_NAME
			"[])) ON CONFLICT DO NOTHING RETURNING tag_id, namespace_id, subtag_id",
			std::forward< std::vector< NamespaceID > >( namespace_ids ),
			std::forward< std::vector< SubtagID > >( subtag_ids ) ) };

		for ( const auto& row : new_tag_ids )
		{
			const auto tag_id { row[ 0 ].as< TagID >() };
			FGL_ASSERT( tag_id > 0, "Tag ID was not greater then zero!" );
			const auto namespace_id { row[ 1 ].as< NamespaceID >() };
			const auto subtag_id { row[ 2 ].as< SubtagID >() };
			map.emplace( std::make_pair( namespace_id, subtag_id ), tag_id );
		}
	}

	for ( const auto& row : select_result )
	{
		// Tag was created in the previous step, so skip it
		if ( row[ 0 ].isNull() ) continue;
		const auto tag_id { row[ 0 ].as< TagID >() };
		FGL_ASSERT( tag_id > 0, "Tag ID was not greater then zero!" );
		const auto namespace_id { row[ 1 ].as< NamespaceID >() };
		const auto subtag_id { row[ 2 ].as< SubtagID >() };
		map.emplace( std::make_pair( namespace_id, subtag_id ), tag_id );
	}

	co_return map;
}

drogon::Task< std::expected< std::vector< TagID >, drogon::HttpResponsePtr > > createTagsFromPairs(
	const std::vector< std::pair< std::string, std::string > >& tag_pairs,
	const DbClientPtr db )
{
	logging::ScopedTimer timer { "createTags" };

	if ( tag_pairs.empty() )
	{
		co_return std::unexpected( createBadRequest( "No tags to create" ) );
	}

	std::vector< TagID > tag_ids {};
	tag_ids.reserve( tag_pairs.size() );

	std::set< std::string > namespace_set {};
	std::set< std::string > subtag_set {};

	for ( const auto& [ namespace_text, subtag_text ] : tag_pairs )
	{
		namespace_set.emplace( namespace_text );
		subtag_set.emplace( subtag_text );
	}

	std::unordered_map< std::string, NamespaceID > namespace_map { co_await getNamespaces( namespace_set, db ) };
	std::unordered_map< std::string, SubtagID > subtag_map { co_await getSubtags( subtag_set, db ) };

	std::vector< NamespaceID > namespace_ids {};
	std::vector< SubtagID > subtag_ids {};

	for ( const auto& [ namespace_text, subtag_text ] : tag_pairs )
	{
		const auto namespace_id { namespace_map.at( namespace_text ) };
		const auto subtag_id { subtag_map.at( subtag_text ) };

		namespace_ids.emplace_back( namespace_id );
		subtag_ids.emplace_back( subtag_id );
	}

	std::unordered_map< std::pair< NamespaceID, SubtagID >, TagID > tag_map {
		co_await getTags( namespace_ids, subtag_ids, db )
	};

	if ( tag_map.size() != namespace_ids.size() )
	{
		co_return std::unexpected( createBadRequest( "Tag count mismatch" ) );
	}

	for ( const auto& [ namespace_text, subtag_text ] : tag_pairs )
	{
		const auto namespace_id { namespace_map.at( namespace_text ) };
		const auto subtag_id { subtag_map.at( subtag_text ) };

		const auto tag_id { tag_map.at( std::make_pair( namespace_id, subtag_id ) ) };

		FGL_ASSERT( tag_id > 0, "Tag ID was not valid" );

		tag_ids.emplace_back( tag_id );
	}

	co_return tag_ids;
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
