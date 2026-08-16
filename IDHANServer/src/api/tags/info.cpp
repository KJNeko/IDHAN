#include "IDHANTypes.hpp"
#include "api/TagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "drogon/HttpResponse.h"
#include "logging/format_ns.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > TagAPI::getTagInfo(
	[[maybe_unused]] const drogon::HttpRequestPtr request,
	const TagID tag_id )
{
	Json::Value root {};
	root[ "tag_id" ] = tag_id;

	const auto db { drogon::app().getDbClient() };

	const auto result { co_await db->execSqlCoro(
		"SELECT t.namespace_id, tn.namespace_text, t.subtag_text, "
		"       COALESCE(SUM(tc.storage_count), 0)::bigint AS storage_count "
		"FROM tags t "
		"JOIN tag_namespaces tn ON tn.namespace_id = t.namespace_id "
		"LEFT JOIN tag_counts tc ON tc.tag_id = t.tag_id "
		"WHERE t.tag_id = $1 "
		"GROUP BY t.namespace_id, tn.namespace_text, t.subtag_text",
		tag_id ) };

	if ( result.empty() )
	{
		co_return createBadRequest(
			"TagID {} was not found. Either you tried to request it before it was committed, or it does not exist",
			tag_id );
	}

	root[ "namespace" ][ "id" ] = result[ 0 ][ "namespace_id" ].as< NamespaceID >();
	root[ "namespace" ][ "text" ] = result[ 0 ][ "namespace_text" ].as< std::string >();
	root[ "subtag" ][ "text" ] = result[ 0 ][ "subtag_text" ].as< std::string >();
	root[ "items_count" ] = result[ 0 ][ "storage_count" ].as< std::size_t >();

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

} // namespace idhan::api
