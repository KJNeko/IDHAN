//
// Created by kj16609 on 11/5/25.
//

#include <expected>

#include "IDHANTypes.hpp"
#include "api/FileRelationshipsAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"

namespace idhan::api
{

namespace
{

std::expected< std::pair< RecordID, RecordID >, drogon::HttpResponsePtr > parseDuplicatePair(
	const Json::Value& object )
{
	// isMember() throws Json::LogicError on non-objects, which would surface as a 500
	if ( !object.isObject() )
		return std::unexpected( createBadRequest( "Expected object with `worse_id` and `better_id`" ) );

	if ( !object.isMember( "worse_id" ) || !object.isMember( "better_id" ) )
		return std::unexpected( createBadRequest( "Expected object to have `worse_id` and `better_id`" ) );

	if ( !object[ "worse_id" ].isUInt() || !object[ "better_id" ].isUInt() )
		return std::unexpected( createBadRequest( "Expected `worse_id` and `better_id` to be unsigned integers" ) );

	const RecordID worse_id { object[ "worse_id" ].as< RecordID >() };
	const RecordID better_id { object[ "better_id" ].as< RecordID >() };

	if ( worse_id == better_id )
		return std::unexpected( createBadRequest( "`worse_id` and `better_id` cannot be the same record" ) );

	return std::pair { worse_id, better_id };
}

drogon::Task< drogon::HttpResponsePtr > insertDuplicatePairs(
	const std::vector< std::pair< RecordID, RecordID > > pairs )
{
	auto db { drogon::app().getDbClient() };

	std::vector< RecordID > referenced_records {};
	referenced_records.reserve( pairs.size() * 2 );
	for ( const auto& [ worse_id, better_id ] : pairs )
	{
		referenced_records.push_back( worse_id );
		referenced_records.push_back( better_id );
	}

	// unknown IDs would otherwise surface as FK-violation 500s
	const auto validation { co_await helpers::validateRecordIds( std::move( referenced_records ), db ) };
	if ( !validation ) co_return validation.error();

	for ( const auto& [ worse_id, better_id ] : pairs )
	{
		try
		{
			co_await db->execSqlCoro( "SELECT insert_duplicate_pair($1, $2)", worse_id, better_id );
		}
		catch ( const std::exception& e )
		{
			// insert_duplicate_pair raises with these exact texts (migration 130); those are
			// conflicts with existing relationships, anything else is a server fault
			const std::string_view what { e.what() };
			if ( what.find( "already inserted worse record" ) != std::string_view::npos
			     || what.find( "would result in a cyclic chain" ) != std::string_view::npos )
				co_return createConflict(
					"Failed to set record {} as a worse duplicate of {}: {}", worse_id, better_id, e.what() );

			co_return createInternalError(
				"Failed to set record {} as a worse duplicate of {}: {}", worse_id, better_id, e.what() );
		}
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( {} );
}

} // namespace

drogon::Task< drogon::HttpResponsePtr > FileRelationshipsAPI::setBetterDuplicate( const drogon::HttpRequestPtr request )
{
	const auto json_ptr { request->getJsonObject() };
	if ( !json_ptr ) co_return createBadRequest( "Expected json body" );
	const auto& json { *json_ptr };

	std::vector< std::pair< RecordID, RecordID > > pairs {};

	if ( json.isArray() )
	{
		pairs.reserve( json.size() );

		for ( const auto& object : json )
		{
			const auto pair { parseDuplicatePair( object ) };
			if ( !pair ) co_return pair.error();
			pairs.emplace_back( *pair );
		}
	}
	else if ( json.isObject() )
	{
		const auto pair { parseDuplicatePair( json ) };
		if ( !pair ) co_return pair.error();
		pairs.emplace_back( *pair );
	}
	else
	{
		co_return createBadRequest(
			"Expected json body of either array of objects, or a single object. Objects must have `better_id` and `worse_id`" );
	}

	co_return co_await insertDuplicatePairs( std::move( pairs ) );
}

} // namespace idhan::api
