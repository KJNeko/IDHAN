// POST /records/metadata: the same info object as /records/{id}/info, for a list of ids, built by
// the shared collector in one query per metadata table.

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "metadata/metadata.hpp"

namespace idhan::api
{

//! Upper bound on ids per request. Bounds memory and query planning; the client windows its fetches.
constexpr std::size_t max_record_ids { 1000 };

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

	auto batch { co_await metadata::collectRecordInfo( std::move( record_ids ), drogon::app().getDbClient() ) };

	Json::Value missing { Json::arrayValue };
	for ( const auto record_id : batch.missing ) missing.append( record_id );

	Json::Value out {};
	out[ "records" ] = std::move( batch.records );
	out[ "missing" ] = std::move( missing );
	co_return drogon::HttpResponse::newHttpJsonResponse( std::move( out ) );
}

} // namespace idhan::api
