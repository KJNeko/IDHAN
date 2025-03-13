//
// Created by kj16609 on 11/15/24.
//

#include <strstream>

#include "api/IDHANRecordAPI.hpp"
#include "api/helpers/records.hpp"
#include "codes/ImportCodes.hpp"
#include "core/files/mime.hpp"
#include "crypto/sha256.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan::api
{

Json::Value createDeletedResponse( const RecordID record_id, const std::size_t deleted_time )
{
	Json::Value root {};

	root[ "record_id" ] = record_id;
	root[ "deleted_time" ] = deleted_time;
	root[ "status" ] = Deleted;

	return root;
}

Json::Value createAlreadyImportedResponse( const RecordID record_id, const std::size_t import_time )
{
	Json::Value root {};

	root[ "record_id" ] = record_id;
	root[ "import_time" ] = import_time;
	root[ "status" ] = Exists;

	return root;
}

Json::Value createUnknownMimeResponse()
{
	Json::Value root {};

	root[ "status" ] = Failed;
	root[ "reason" ] = "unknown mime";
	root[ "reason_id" ] = UnknownMime;

	return root;
}

/*
drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::importFile( const drogon::HttpRequestPtr request )
{
	log::debug( "Hit" );
	FGL_ASSERT( request, "Request invalid" );
	const auto request_data { request->getBody() };
	const auto content_type { request->getContentType() };
	log::debug( "Body length: {}", request_data.size() );
	log::debug( "Content type: {}", static_cast< int >( content_type ) );

	auto db { drogon::app().getDbClient() };

	const SHA256 sha256 { SHA256::hash( request_data ) };

	const std::optional< std::string > mime_str { mime::getInstance()->scan( request_data ) };
	log::debug( "MIME type: {}", mime_str.value_or( "NONE" ) );

	// We've never seen this file before, So we can import it.

	if ( !mime_str.has_value() )
	{
		co_return drogon::HttpResponse::newHttpJsonResponse( createUnknownMimeResponse() );
	}

	const auto record_id { co_await api::createRecord( sha256, db ) };

	const auto deleted_result {
		co_await db->execSqlCoro( "SELECT deleted_time FROM deleted_files WHERE record_id = $1 LIMIT 1", record_id )
	};

	if ( deleted_result.size() > 0 )
	{
		// file was deleted, we can simply return now.
		co_return drogon::HttpResponse::
			newHttpJsonResponse( createDeletedResponse( record_id, deleted_result[ 0 ][ 0 ].as< std::size_t >() ) );
	}

	// file was not deleted, so we should check if it's in the cluster. if not we should save it there.

	//TODO: Get cluster

	Json::Value root {};

	root[ "status" ] = 200;

	const auto response { drogon::HttpResponse::newHttpJsonResponse( root ) };

	co_return response;
}
*/

} // namespace idhan::api
