#include "IDHANTypes.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "db/dbTypes.hpp"
#include "db/drogonArrayBind.hpp"
#include "drogon/utils/coroutine.h"
#include "splitTag.hpp"
#include "threading/ExpectedTask.hpp"
#include "tags/tags.hpp"

namespace idhan
{

ExpectedTask< std::unordered_map< std::string, TagID > > mapTags(
	const std::vector< std::string >& tags,
	DbClientPtr db )
{
	std::vector< std::string > namespaces {};
	std::vector< std::string > subtags {};
	namespaces.reserve( tags.size() );
	subtags.reserve( tags.size() );
	for ( const auto& tag : tags )
	{
		auto [ tag_namespace, tag_subtag ] = splitTag( tag );
		namespaces.emplace_back( std::move( tag_namespace ) );
		subtags.emplace_back( std::move( tag_subtag ) );
	}

	constexpr auto query {
		"SELECT tags.tag_id AS tag_id, input.ord AS ord "
		"FROM UNNEST($1::TEXT[], $2::TEXT[]) WITH ORDINALITY AS input(namespace_text, subtag_text, ord) "
		"JOIN tag_namespaces ON tag_namespaces.namespace_text = input.namespace_text "
		"JOIN tags ON tags.namespace_id = tag_namespaces.namespace_id AND tags.subtag_text = input.subtag_text"
	};

	const auto tag_id_result { co_await db->execSqlCoro( query, std::move( namespaces ), std::move( subtags ) ) };

	std::unordered_map< std::string, TagID > tag_ids_result {};

	for ( const auto& row : tag_id_result )
	{
		// ORDINALITY is 1-based; map the row back to the exact input tag string it resolved from
		const auto ord { row[ "ord" ].as< std::size_t >() };
		tag_ids_result.emplace( tags[ ord - 1 ], row[ "tag_id" ].as< TagID >() );
	}

	for ( const auto& tag : tags )
		if ( !tag_ids_result.contains( tag ) ) [[unlikely]]
			co_return std::unexpected( createNotFound( "Was unable to get ID for tag {}, Tag does not exist", tag ) );

	co_return tag_ids_result;
}

ExpectedTask< void > associateTags(
	const RecordID record_id,
	const std::vector< std::string >& tags,
	const std::string_view tag_domain,
	DbClientPtr db )
{
	if ( tags.empty() ) co_return {};

	const auto domain { co_await findTagDomain( tag_domain, db ) };
	if ( !domain )
		co_return std::unexpected( createNotFound( "Downloader tag domain '{}' does not exist", tag_domain ) );

	std::vector< std::string > namespaces {};
	std::vector< std::string > subtags {};
	namespaces.reserve( tags.size() );
	subtags.reserve( tags.size() );

	for ( const auto& tag : tags )
	{
		auto [ tag_namespace, tag_subtag ] { splitTag( tag ) };
		namespaces.emplace_back( std::move( tag_namespace ) );
		subtags.emplace_back( std::move( tag_subtag ) );
	}

	const auto created { co_await db->execSqlCoro(
		"SELECT tag_id FROM createBatchTags($1::TEXT[], $2::TEXT[])", std::move( namespaces ), std::move( subtags ) ) };
	std::vector< TagID > tag_ids {};
	tag_ids.reserve( created.size() );
	for ( const auto& row : created ) tag_ids.emplace_back( row[ "tag_id" ].as< TagID >() );

	co_await db->execSqlCoro(
		"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) "
		"SELECT $1, unnest($2::" TAG_PG_TYPE_NAME "[]), $3 ON CONFLICT DO NOTHING",
		record_id,
		std::move( tag_ids ),
		domain->id );
	co_return {};
}

} // namespace idhan
