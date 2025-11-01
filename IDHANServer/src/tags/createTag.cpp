//
// Created by kj16609 on 10/27/25.
//

#include "drogon/HttpResponse.h"
#include "tags.hpp"

namespace idhan
{
drogon::Task< std::optional< TagID > >
	findTag( const NamespaceID namespace_id, const SubtagID subtag_id, drogon::orm::DbClientPtr db )
{
	const auto search_result { co_await db->execSqlCoro(
		"SELECT tag_id FROM tags WHERE namespace_id = $1 AND subtag_id = $2", namespace_id, subtag_id ) };

	if ( search_result.empty() ) co_return std::nullopt;

	co_return search_result[ 0 ][ 0 ].as< TagID >();
}

drogon::Task< std::expected< TagID, IDHANError > >
	createTag( const std::string tag_namespace, const std::string tag_subtag, drogon::orm::DbClientPtr db )
{
	auto namespace_id_t { createNamespace( tag_namespace, db ) };
	auto subtag_id_t { createSubtag( tag_subtag, db ) };
	const auto [ namespace_id, subtag_id ] =
		co_await drogon::when_all( std::move( namespace_id_t ), std::move( subtag_id_t ) );

	if ( !namespace_id ) co_return std::unexpected( namespace_id.error() );
	if ( !subtag_id ) co_return std::unexpected( subtag_id.error() );

	const auto search_result { co_await findTag( *namespace_id, *subtag_id, db ) };

	if ( search_result ) co_return *search_result;

	const auto insert_result { co_await db->execSqlCoro(
		"INSERT INTO tags (namespace_id, subtag_id) VALUES ($1, $2) RETURNING tag_id", *namespace_id, *subtag_id ) };

	if ( insert_result.empty() )
		co_return std::unexpected( createError( "Failed to create tag: {}:{}", *namespace_id, *subtag_id ) );

	co_return insert_result[ 0 ][ 0 ].as< TagID >();
}

} // namespace idhan
