//
// Created by kj16609 on 3/13/25.
//

#include "tags.hpp"

#include "api/helpers/createBadRequest.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"
#include "splitTag.hpp"

namespace idhan
{

drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > >
	findOrCreateTag( const NamespaceID namespace_id, const SubtagID subtag_id, drogon::orm::DbClientPtr db )
{
	const auto search { co_await db->execSqlCoro(
		"SELECT tag_id FROM tags WHERE namespace_id = $1 AND subtag_id = $2", namespace_id, subtag_id ) };

	if ( search.size() > 0 )
	{
		co_return search[ 0 ][ 0 ].as< TagID >();
	}

	try
	{
		const auto insert_result { co_await db->execSqlCoro(
			"INSERT INTO tags (namespace_id, subtag_id) VALUES ($1, $2) RETURNING tag_id", namespace_id, subtag_id ) };

		if ( insert_result.empty() )
			co_return std::unexpected( createInternalError( "Failed to create tag {}:{}", namespace_id, subtag_id ) );

		co_return insert_result[ 0 ][ 0 ].as< TagID >();
	}
	catch ( std::exception& e )
	{
		co_return std::
			unexpected( createInternalError( "Failed to create tag {}:{}: {}", namespace_id, subtag_id, e.what() ) );
	}
}

drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > >
	findOrCreateTag( const std::string tag_text, drogon::orm::DbClientPtr db )
{
	const auto split_tag { splitTag( tag_text ) };
	const auto& [ namespace_text, subtag_text ] = split_tag;

	auto transaction { co_await db->newTransactionCoro() };

	const auto namespace_id { co_await findOrCreateNamespace( namespace_text, transaction ) };
	const auto subtag_id { co_await findOrCreateSubtag( subtag_text, transaction ) };

	if ( !namespace_id.has_value() ) co_return std::unexpected( namespace_id.error() );
	if ( !subtag_id.has_value() ) co_return std::unexpected( subtag_id.error() );

	const auto tag_result { co_await findOrCreateTag( namespace_id.value(), subtag_id.value(), transaction ) };

	if ( tag_result.has_value() ) co_return tag_result;

	transaction->rollback();
}

} // namespace idhan
