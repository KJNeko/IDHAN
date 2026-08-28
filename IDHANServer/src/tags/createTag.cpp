#include "drogon/HttpResponse.h"
#include "tags.hpp"

namespace idhan
{
drogon::Task< std::optional< TagID > > findTag(
	const NamespaceID namespace_id,
	const std::string subtag_text,
	drogon::orm::DbClientPtr db )
{
	const auto search_result { co_await db->execSqlCoro(
		"SELECT tag_id FROM tags WHERE namespace_id = $1 "
		"AND subtag_text = NORMALIZE(CASEFOLD(NORMALIZE($2, NFC)), NFC)",
		namespace_id,
		subtag_text ) };

	if ( search_result.empty() ) co_return std::nullopt;

	co_return search_result[ 0 ][ 0 ].as< TagID >();
}

drogon::Task< std::expected< TagID, IDHANError > > createTag(
	const std::string tag_namespace,
	const std::string tag_subtag,
	drogon::orm::DbClientPtr db )
{
	const auto namespace_id { co_await createNamespace( tag_namespace, db ) };

	if ( !namespace_id ) co_return std::unexpected( namespace_id.error() );

	co_return co_await createTag( *namespace_id, tag_subtag, db );
}

drogon::Task< std::expected< TagID, IDHANError > > createTag(
	const NamespaceID namespace_id,
	const std::string tag_subtag,
	drogon::orm::DbClientPtr db )
{
	if ( const auto search_result { co_await findTag( namespace_id, tag_subtag, db ) } ) co_return *search_result;

	const auto insert_result { co_await db->execSqlCoro(
		"INSERT INTO tags (namespace_id, subtag_text) VALUES ($1, $2) "
		"ON CONFLICT (namespace_id, subtag_text) DO UPDATE SET subtag_text = EXCLUDED.subtag_text "
		"RETURNING tag_id",
		namespace_id,
		tag_subtag ) };

	if ( insert_result.empty() )
		co_return std::unexpected( createError( "Failed to create tag: {}:{}", namespace_id, tag_subtag ) );

	co_return insert_result[ 0 ][ 0 ].as< TagID >();
}

} // namespace idhan
