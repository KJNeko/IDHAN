#include <algorithm>

#include "IDHANTypes.hpp"
#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "db/drogonArrayBind.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::addAlternative( drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	const auto json_ptr { request->getJsonObject() };
	if ( !json_ptr ) co_return createBadRequest( "Expected json body" );

	const auto& json { *json_ptr };

	if ( !json.isArray() ) co_return createBadRequest( "Expected json array of integers" );

	if ( json.size() < 2 ) co_return createBadRequest( "Expected at least 2 record ids to pair as alternatives" );

	std::vector< RecordID > record_ids {};

	for ( const auto& id : json )
	{
		if ( !id.isUInt() ) co_return createBadRequest( "Expected json array of unsigned integers" );

		record_ids.emplace_back( id.as< RecordID >() );
	}

	std::ranges::sort( record_ids );
	const auto repeated { std::ranges::unique( record_ids ) };
	record_ids.erase( repeated.begin(), repeated.end() );

	if ( record_ids.size() < 2 ) co_return createBadRequest( "Expected at least 2 distinct record ids" );

	// unknown records would otherwise surface as FK-violation 500s in the insert
	const auto validation { co_await helpers::validateRecordIds( record_ids, db ) };
	if ( !validation ) co_return validation.error();

	// Every listed record is an alternative of every other, written out pair by pair: the mapping is
	// direct, so nothing here reaches records paired with these elsewhere.
	std::vector< RecordID > lesser {};
	std::vector< RecordID > greater {};

	for ( std::size_t i = 0; i < record_ids.size(); ++i )
		for ( std::size_t j = i + 1; j < record_ids.size(); ++j )
		{
			lesser.emplace_back( record_ids[ i ] );
			greater.emplace_back( record_ids[ j ] );
		}

	co_await db->execSqlCoro(
		"INSERT INTO alternative_records (lesser_record_id, greater_record_id) "
		"SELECT * FROM UNNEST($1::" RECORD_PG_TYPE_NAME "[], $2::" RECORD_PG_TYPE_NAME "[]) "
		"ON CONFLICT DO NOTHING",
		std::move( lesser ),
		std::move( greater ) );

	co_return drogon::HttpResponse::newHttpJsonResponse( {} );
}

} // namespace idhan::api
