//
// Created by kj16609 on 7/17/26.
//
// POST /records/metadata — batch metadata for many records in one query. A grid scrolling tens of
// thousands of results cannot call /records/{id}/info per tile; this returns the same "basic" shape
// for a list of ids in a single set-based query.

#include <unordered_set>

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "db/drogonArrayBind.hpp"

namespace idhan::api
{

namespace
{
//! Upper bound on ids per request. Bounds memory and query planning; the client windows its fetches.
constexpr std::size_t max_record_ids { 1000 };
} // namespace

drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchMetadataBatch( drogon::HttpRequestPtr request )
{
	const auto body { request->getJsonObject() };
	if ( !body || !body->isObject() ) co_return createBadRequest( "Request body must be a JSON object" );
	const Json::Value& json { *body };

	if ( !json.isMember( "record_ids" ) || !json[ "record_ids" ].isArray() )
		co_return createBadRequest( "'record_ids' must be an array of integers" );

	if ( json[ "record_ids" ].size() > max_record_ids )
		co_return createBadRequest( "Too many record_ids: {} (max {})", json[ "record_ids" ].size(), max_record_ids );

	std::vector< RecordID > record_ids {};
	record_ids.reserve( json[ "record_ids" ].size() );
	for ( const auto& id : json[ "record_ids" ] )
	{
		if ( !id.isIntegral() ) co_return createBadRequest( "'record_ids' must be an array of integers" );
		record_ids.emplace_back( static_cast< RecordID >( id.asInt64() ) );
	}

	// "include" is accepted for forward compatibility; only "basic" is implemented today. Tags/urls/
	// notes are deliberately excluded from a batch fetch — on a large grid they would dwarf the
	// payload — and file-specific metadata (dimensions, duration) remains per-record for now.
	if ( json.isMember( "include" ) && !json[ "include" ].isArray() )
		co_return createBadRequest( "'include' must be an array of strings" );

	// Init as arrays so empty results serialise as [] rather than null.
	Json::Value records { Json::arrayValue };
	Json::Value missing { Json::arrayValue };

	if ( !record_ids.empty() )
	{
		const auto db { drogon::app().getDbClient() };

		// One query for the whole batch. LEFT JOINs so a record with no file_info still comes back
		// (with a null mime) rather than vanishing.
		const auto result { co_await db->execSqlCoro(
			"SELECT r.record_id, r.sha256, fi.size, fi.mime_id, m.name, m.best_extension "
			"FROM records r "
			"LEFT JOIN file_info fi ON fi.record_id = r.record_id "
			"LEFT JOIN mime m ON m.mime_id = fi.mime_id "
			"WHERE r.record_id = ANY($1::INTEGER[])",
			std::forward< const std::vector< RecordID > >( record_ids ) ) };

		std::unordered_set< RecordID > found {};
		found.reserve( result.size() );

		for ( const auto& row : result )
		{
			const auto record_id { row[ "record_id" ].as< RecordID >() };
			found.insert( record_id );

			Json::Value entry {};
			entry[ "record_id" ] = record_id;
			entry[ "hashes" ][ "sha256" ] = SHA256::fromPgCol( row[ "sha256" ] ).hex();

			if ( !row[ "mime_id" ].isNull() )
			{
				entry[ "size" ] = row[ "size" ].as< std::size_t >();
				entry[ "mime" ] = row[ "name" ].as< std::string >();
				entry[ "extension" ] = row[ "best_extension" ].as< std::string >();
			}

			records.append( std::move( entry ) );
		}

		for ( const auto record_id : record_ids )
			if ( !found.contains( record_id ) ) missing.append( record_id );
	}

	Json::Value out {};
	out[ "records" ] = std::move( records );
	out[ "missing" ] = std::move( missing );
	co_return drogon::HttpResponse::newHttpJsonResponse( out );
}

} // namespace idhan::api
