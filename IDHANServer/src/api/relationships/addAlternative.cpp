//
// Created by kj16609 on 11/5/25.
//

#include "IDHANTypes.hpp"
#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "db/drogonArrayBind.hpp"

namespace idhan::api
{

using GroupID = idhan::Integer;

drogon::Task< GroupID > createNewGroup( DbClientPtr db )
{
	const auto group_result {
		co_await db->execSqlCoro( "INSERT INTO alternative_groups DEFAULT VALUES RETURNING group_id" )
	};

	const auto group_id { group_result[ 0 ][ 0 ].as< GroupID >() };
	co_return group_id;
}

drogon::Task<> addItemsToNewGroup( std::vector< RecordID > record_ids, DbClientPtr db )
{
	const auto group_id { co_await createNewGroup( db ) };
	co_await db->execSqlCoro(
		"INSERT INTO alternative_group_members (group_id, record_id) VALUES ($1, UNNEST($2::" RECORD_PG_TYPE_NAME
		"[]))",
		group_id,
		std::move( record_ids ) );
}

drogon::Task<> addItemsToExistingGroup(
	std::vector< RecordID > record_ids,
	const std::vector< GroupID > group_ids,
	drogon::orm::DbClientPtr db )
{
	const auto group_id { group_ids[ 0 ] };

	co_await db->execSqlCoro(
		"INSERT INTO alternative_group_members (group_id, record_id) VALUES ($1, UNNEST($2::" RECORD_PG_TYPE_NAME
		"[])) ON CONFLICT (group_id, record_id) DO NOTHING",
		group_id,
		std::move( record_ids ) );
}

drogon::Task<> addItemsToExistingGroupsMerge(
	std::vector< RecordID > record_ids,
	std::vector< GroupID > group_ids,
	drogon::orm::DbClientPtr db )
{
	const auto group_id { co_await createNewGroup( db ) };

	// group_ids is bound again in the DELETE below; binding an rvalue hands the vector
	// to the SqlBinder, so the first use must be a copy
	co_await db->execSqlCoro(
		"UPDATE alternative_group_members SET group_id = $1 WHERE group_id = ANY($2)",
		group_id,
		std::vector< GroupID >( group_ids ) );

	co_await db->execSqlCoro(
		"INSERT INTO alternative_group_members (group_id, record_id) VALUES ($1, UNNEST($2::" RECORD_PG_TYPE_NAME
		"[])) ON CONFLICT (group_id, record_id) DO NOTHING",
		group_id,
		std::move( record_ids ) );

	co_await db->execSqlCoro(
		"DELETE FROM alternative_groups WHERE group_id = ANY($1)",
		std::move( group_ids ) );
}

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::addAlternative( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	const auto json_ptr { request->getJsonObject() };
	if ( !json_ptr ) co_return createBadRequest( "Expected json body" );

	const auto& json { *json_ptr };

	if ( !json.isArray() ) co_return createBadRequest( "Expected json array of integers" );

	if ( json.size() < 2 ) co_return createBadRequest( "Expected at least 2 record ids to form an alternative group" );

	std::vector< RecordID > record_ids {};

	for ( const auto& id : json )
	{
		if ( !id.isUInt() ) co_return createBadRequest( "Expected json array of unsigned integers" );

		record_ids.emplace_back( id.as< RecordID >() );
	}

	// unknown records would otherwise surface as FK-violation 500s in the member inserts
	const auto validation { co_await helpers::validateRecordIds( record_ids, db ) };
	if ( !validation ) co_return validation.error();

	// record_ids is reused below; binding an rvalue hands the vector to the SqlBinder,
	// so pass a copy here
	const auto group_search { co_await db->execSqlCoro(
		"SELECT DISTINCT group_id FROM alternative_group_members WHERE record_id = ANY($1)",
		std::vector< RecordID >( record_ids ) ) };

	std::vector< GroupID > group_ids {};

	for ( const auto& id : group_search )
	{
		group_ids.emplace_back( id[ 0 ].as< GroupID >() );
	}

	std::ranges::sort( group_ids );
	const auto duplicates { std::ranges::unique( group_ids ) };
	group_ids.erase( duplicates.begin(), duplicates.end() );

	const bool no_existing_groups { group_ids.empty() };
	const bool existing_groups { not no_existing_groups };
	const bool multiple_groups { not no_existing_groups && group_ids.size() > 1 };

	if ( no_existing_groups )
	{
		co_await addItemsToNewGroup( record_ids, db );
	}
	else if ( existing_groups && !multiple_groups )
	{
		co_await addItemsToExistingGroup( record_ids, group_ids, db );
	}
	else if ( multiple_groups )
	{
		co_await addItemsToExistingGroupsMerge( record_ids, group_ids, db );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( {} );
}

} // namespace idhan::api
