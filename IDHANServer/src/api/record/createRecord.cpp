//
// Created by kj16609 on 11/17/24.
//

#include "IDHANTypes.hpp"
#include "api/IDHANRecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/defines.hpp"
#include "logging/ScopedTimer.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

ResponseTask createRecordFromOctet( const drogon::HttpRequestPtr req )
{
	co_return createBadRequest( "Not implemented" );
}

ResponseTask createRecordFromSHA256( const SHA256 sha256, drogon::orm::DbClientPtr db )
{}

drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	createRecords( const std::vector< SHA256 >& hashes, drogon::orm::DbClientPtr db )
{}

drogon::Task< void > processBatchAndMapRecords(
	std::unordered_set< SHA256 >& hashes,
	std::unordered_map< SHA256, RecordID >& record_id_map,
	drogon::orm::DbClientPtr db )
{
	std::vector< SHA256 > hashes_vec {};
	hashes_vec.insert( hashes_vec.begin(), hashes.begin(), hashes.end() );

	const auto record_ids { co_await helpers::massCreateRecord( hashes_vec, db ) };

	for ( std::size_t i = 0; i < hashes_vec.size(); ++i )
	{
		const auto& hash { hashes_vec[ i ] };
		const auto& record_id { record_ids[ i ] };
		record_id_map.emplace( hash, record_id );
	}

	hashes.clear();
}

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	createRecordsFromJsonArray( const Json::Value& json, drogon::orm::DbClientPtr db )
{
	std::unordered_set< SHA256 > hashes {};

	std::unordered_map< SHA256, RecordID > record_id_map {};
	std::vector< SHA256 > hashes_order {};

	for ( const auto& value : json )
	{
		if ( !value.isString() )
			co_return std::unexpected( createBadRequest( "Json value in array must be a string" ) );

		const auto& str { value.asString() };

		//insert the hash into a set to make each hash unique

		const auto expected_hash { SHA256::fromHex( str ) };
		if ( !expected_hash ) co_return std::unexpected( expected_hash.error() );

		hashes.insert( expected_hash.value() );

		if ( hashes.size() >= 100 )
		{
			co_await processBatchAndMapRecords( hashes, record_id_map, db );
		}

		hashes_order.emplace_back( expected_hash.value() );
	}

	if ( hashes_order.size() != record_id_map.size() )
	{
		log::warn(
			"Create records endpoint got {} hashes but only {} were unique, This impacts performance, Only submit unique hashes",
			hashes_order.size(),
			record_id_map.size() );
	}

	if ( hashes.size() > 0 )
	{
		co_await processBatchAndMapRecords( hashes, record_id_map, db );
	}

	Json::Value json_array {};
	Json::ArrayIndex idx { 0 };

	for ( const auto& value : hashes_order )
	{
		json_array[ idx++ ] = record_id_map[ value ];
	}

	co_return json_array;
}

ResponseTask createRecordFromJson( const drogon::HttpRequestPtr req )
{
	logging::ScopedTimer timer { "createRecordFromJson" };
	const auto json_ptr { req->getJsonObject() };
	if ( json_ptr == nullptr ) // Data was invalid?
		throw std::invalid_argument( "json_ptr is null" );

	const Json::Value& json { *json_ptr };

	auto db { drogon::app().getDbClient() };

	//test if sha256 is a list or 1 item
	const auto& sha256s { json[ "sha256" ] };
	if ( sha256s.isArray() )
	{
		const auto json_result { co_await createRecordsFromJsonArray( sha256s, db ) };

		if ( !json_result ) co_return json_result.error();

		const auto& json_array { json_result.value() };

		co_return drogon::HttpResponse::newHttpJsonResponse( json_array );
	}
	else if ( sha256s.isString() ) // HEX string
	{
		Json::Value json_out {};
		const auto sha256 { SHA256::fromHex( sha256s.asString() ) };

		if ( !sha256 ) co_return sha256.error();

		const RecordID record_id { co_await helpers::createRecord( *sha256, db ) };
		json_out[ "record_id" ] = record_id;

		co_return drogon::HttpResponse::newHttpJsonResponse( json_out );
	}

	FGL_UNREACHABLE();
}

ResponseTask IDHANRecordAPI::createRecord( const drogon::HttpRequestPtr request )
{
	logging::ScopedTimer timer { "createRecord" };
	// the request here should be either an octet stream, or json. If it's an octet stream, then it will be a file we can hash.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
	// It's really not possible for us to have every single enum here.
	switch ( request->getContentType() )
	{
		// Here we are expecting that the data being shoved into the request is actually a file.
		case drogon::CT_APPLICATION_OCTET_STREAM:
			co_return co_await createRecordFromOctet( request );
			break;
		// In this case we have either a list of hashes, or a single hash
		case drogon::CT_APPLICATION_JSON:
			co_return co_await createRecordFromJson( request );
			break;
		default:
			//TODO: Failure
			break;
	}
#pragma GCC diagnostic pop

	co_return createBadRequest( "Unexpected content type" );
}

} // namespace idhan::api
