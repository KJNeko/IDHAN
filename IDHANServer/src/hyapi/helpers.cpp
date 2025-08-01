//
// Created by kj16609 on 7/24/25.
//

#include "helpers.hpp"

#include "IDHANTypes.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/HttpAppFramework.h"
#include "drogon/HttpResponse.h"
#include "drogon/orm/DbClient.h"

namespace idhan::hyapi::helpers
{

drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	idsFromHashesArray( Json::Value json, const drogon::orm::DbClientPtr db )
{
	std::vector< RecordID > records {};

	if ( !json.isArray() ) co_return std::unexpected( createInternalError( "Invalid JSON, Expected to be an array" ) );

	for ( Json::ArrayIndex i = 0; i < json.size(); ++i )
	{
		const auto hash { json[ i ].asString() };
		const auto sha256 { SHA256::fromHex( hash ) };
		if ( !sha256 ) co_return std::unexpected( sha256.error() );
		const auto record_id { co_await api::helpers::findRecord( *sha256, db ) };
		if ( !record_id ) co_return std::unexpected( createBadRequest( "Invalid SHA256" ) );
		records.emplace_back( record_id.value() );
	}

	co_return records;
}

drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	extractRecordIDsFromFilesJson( const Json::Value json, const drogon::orm::DbClientPtr db )
{
	std::vector< RecordID > records {};

	if ( !json.isObject() ) co_return std::unexpected( createInternalError( "Invalid JSON, Expected to be a object" ) );

	// check for `hash`
	if ( json.isMember( "hash" ) )
	{
		// Only one item is sent
		const auto hash { json[ "hash" ].asString() };
		const auto sha256 { SHA256::fromHex( hash ) };
		if ( !sha256 ) co_return std::unexpected( sha256.error() );
		const auto record_id { co_await api::helpers::findRecord( *sha256, db ) };
		if ( !record_id ) co_return std::unexpected( createBadRequest( "Invalid SHA256" ) );
		records.emplace_back( record_id.value() );
		co_return records;
	}

	if ( json.isMember( "hashes" ) )
	{
		co_return co_await idsFromHashesArray( json[ "hashes" ], db );
	}

	if ( json.isMember( "record_id" ) )
	{
		const auto record_id { json[ "record_id" ].as< RecordID >() };
		records.emplace_back( record_id );
		co_return records;
	}

	if ( json.isMember( "record_ids" ) )
	{
		for ( Json::ArrayIndex i = 0; i < json.size(); ++i )
		{
			const auto record_id { json[ i ].as< RecordID >() };
			records.emplace_back( record_id );
		}
		co_return records;
	}

	co_return std::unexpected( createBadRequest(
		"Unable to extract record ids: None of the expected hydrus `files` json members were identified" ) );
}

drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	extractRecordIDsFromParameters( drogon::HttpRequestPtr request, const drogon::orm::DbClientPtr db )
{
	if ( auto opt = request->getOptionalParameter< std::string >( "hashes" ) )
	{
		// Json array of hashes
		Json::Reader reader {};
		Json::Value hashes {};
		reader.parse( opt.value(), hashes );

		co_return co_await idsFromHashesArray( hashes, db );
	}

	if ( auto opt = request->getOptionalParameter< std::string >( "file_ids" ) )
	{
		Json::Reader reader {};
		Json::Value file_ids {};
		reader.parse( opt.value(), file_ids );

		std::vector< RecordID > records {};
		for ( const auto& id : file_ids ) records.push_back( id.as< RecordID >() );
		co_return records;
	}

	if ( auto opt = request->getOptionalParameter< std::string >( "hash" ) )
	{
		const auto sha256 { SHA256::fromHex( opt.value() ) };
		if ( !sha256 ) co_return std::unexpected( sha256.error() );
		const auto record_id { co_await api::helpers::findRecord( *sha256, db ) };
		if ( !record_id ) co_return std::unexpected( createBadRequest( "Invalid SHA256" ) );
		std::vector< RecordID > records { record_id.value() };
		co_return records;
	}

	if ( auto opt = request->getOptionalParameter< RecordID >( "file_id" ) )
	{
		std::vector< RecordID > records { opt.value() };
		co_return records;
	}

	co_return std::unexpected(
		createBadRequest( "Unable to extract record ids: None of the expected parameters were identified" ) );
}

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	extractRecordIDsToJsonFromFiles( const Json::Value json, const drogon::orm::DbClientPtr db )
{
	const auto records_e { co_await extractRecordIDsFromFilesJson( json, db ) };
	if ( !records_e ) co_return std::unexpected( records_e.error() );
	const auto& records { records_e.value() };

	Json::Value json_out { Json::ValueType::arrayValue };
	for ( const auto& record : records ) json_out.append( record );
	co_return json_out;
}

} // namespace idhan::hyapi::helpers