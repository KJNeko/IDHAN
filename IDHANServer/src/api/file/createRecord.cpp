//
// Created by kj16609 on 11/17/24.
//

#include "IDHANTypes.hpp"
#include "api/IDHANFileAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "crypto/sha256.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

ResponseTask createRecordFromOctet( const drogon::HttpRequestPtr req )
{}

ResponseTask createRecordFromJson( const drogon::HttpRequestPtr req )
{
	const auto json_ptr { req->getJsonObject() };
	if ( json_ptr == nullptr ) // Data was invalid?
		throw std::invalid_argument( "json_ptr is null" );

	const Json::Value& json { *json_ptr };

	auto db { drogon::app().getDbClient() };

	//test if sha256 is a list or 1 item
	const auto& sha256s { json[ "sha256" ] };
	if ( sha256s.isArray() )
	{
		Json::Value json_array {};
		Json::ArrayIndex idx { 0 };

		for ( const auto& value : sha256s )
		{
			if ( !value.isString() ) co_return createBadRequest( "Json value in array was not a string" );

			const auto& str { value.asString() };

			// dehexify the string.
			SHA256 sha256 { SHA256::fromHex( str ) };

			const RecordID record_id { co_await createRecord( sha256, db ) };
			json_array[ idx++ ] = record_id;
		}

		co_return drogon::HttpResponse::newHttpJsonResponse( json_array );
	}
	else if ( sha256s.isString() ) // HEX string
	{
		Json::Value json_out {};
		SHA256 sha256 { SHA256::fromHex( sha256s.asString() ) };
		const RecordID record_id { co_await createRecord( sha256, db ) };
		json_out[ "record_id" ] = record_id;

		co_return drogon::HttpResponse::newHttpJsonResponse( json_out );
	}
}

ResponseTask IDHANFileAPI::createRecord( const drogon::HttpRequestPtr request )
{
	// the request here should be either an octet stream, or json. If it's an octet stream, then it will be a file we can hash.

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

	co_return createBadRequest( "Unexpected content type" );
}

} // namespace idhan::api
