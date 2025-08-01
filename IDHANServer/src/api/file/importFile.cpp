//
// Created by kj16609 on 11/15/24.
//

#include "api/ImportAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "codes/ImportCodes.hpp"
#include "crypto/SHA256.hpp"
#include "filesystem/ClusterManager.hpp"
#include "logging/log.hpp"
#include "metadata/parseMetadata.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan::api
{

Json::Value createDeletedResponse( const RecordID record_id, const std::size_t deleted_time )
{
	Json::Value root {};

	root[ "record_id" ] = record_id;
	root[ "cluster_delete_time" ] = deleted_time;
	root[ "status" ] = Deleted;

	return root;
}

Json::Value createAlreadyImportedResponse( const RecordID record_id, const std::size_t import_time )
{
	Json::Value root {};

	root[ "record_id" ] = record_id;
	root[ "cluster_store_time" ] = import_time;
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

drogon::Task< drogon::HttpResponsePtr > ImportAPI::importFile( const drogon::HttpRequestPtr request )
{
	FGL_ASSERT( request, "Request invalid" );
	const auto request_data { request->getBody() };
	const auto content_type { request->getContentType() };

	auto db { drogon::app().getDbClient() };

	const std::byte* data_ptr { reinterpret_cast< const std::byte* >( request_data.data() ) };
	const auto data_length { request_data.size() };

	const SHA256 sha256 { SHA256::hash( data_ptr, data_length ) };

	const auto mime_str { mime::getInstance()->scan( request_data ) };

	const bool overwrite_flag { request->getOptionalParameter< bool >( "overwrite" ).value_or( false ) };
	const bool import_deleted { request->getOptionalParameter< bool >( "import_deleted" ).value_or( false ) };

	if ( !mime_str )
	{
		// If the mime type is not known, Then simply skip it.
		co_return drogon::HttpResponse::newHttpJsonResponse( createUnknownMimeResponse() );
	}

	const bool is_octet { mime_str == INVALID_MIME_NAME };
	const bool force_import { request->getOptionalParameter< bool >( "force" ).value_or( false ) };

	if ( is_octet && !force_import )
	{
		co_return createBadRequest(
			"Mime type not known by IDHAN. Either set the force import flag in the parameters, or teach IDHAN how to detect the mime for this file" );
	}

	const auto mime_id { co_await mime::getIDForStr( mime_str.value(), db ) };

	const auto record_id { co_await helpers::createRecord( sha256, db ) };

	// try to insert info if it's missing
	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, mime_id, size) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING",
		record_id,
		mime_id,
		data_length );

	// select deleted time and store time
	const auto delete_time { co_await db->execSqlCoro(
		"SELECT cluster_delete_time, cluster_store_time, EXTRACT(EPOCH FROM cluster_delete_time)::BIGINT as cluster_delete_time_epoch FROM file_info WHERE record_id = $1 LIMIT 1",
		record_id ) };

	const bool deleted { !delete_time[ 0 ][ "cluster_delete_time" ].isNull() };
	const bool stored {
		!delete_time[ 0 ][ "cluster_store_time" ].isNull() && !overwrite_flag
	}; // if the file is not deleted, it is stored, But if the overwrite flag is on. Store it anyway
	// T (store time is not null) && F (overwrite flag is true) // Not stored

	if ( deleted && !import_deleted )
	{
		// file was deleted, we can simply return now.
		co_return drogon::HttpResponse::newHttpJsonResponse(
			createDeletedResponse( record_id, delete_time[ 0 ][ "cluster_delete_time_epoch" ].as< std::size_t >() ) );
	}

	const bool should_store { ( !deleted && !stored ) || ( !stored && import_deleted ) };

	if ( should_store || overwrite_flag )
	{
		const auto store_result {
			co_await filesystem::ClusterManager::getInstance().storeFile( record_id, data_ptr, data_length, db )
		};

		if ( !store_result )
		{
			co_return store_result.error();
		}
	}

	const auto creation_time { co_await db->execSqlCoro(
		"SELECT creation_time, EXTRACT(EPOCH FROM creation_time)::BIGINT FROM records WHERE record_id = $1 LIMIT 1",
		record_id ) };

	const auto store_time { co_await db->execSqlCoro(
		"SELECT cluster_store_time, EXTRACT(EPOCH FROM cluster_store_time)::BIGINT FROM file_info WHERE record_id = $1 LIMIT 1",
		record_id ) };

	Json::Value root {};

	root[ "status" ] = stored ? ImportStatus::Exists : ImportStatus::Success;
	root[ "record_id" ] = record_id;

	root[ "record" ][ "id" ] = record_id;

	root[ "record" ][ "creation_time_human" ] = creation_time[ 0 ][ 0 ].as< std::string >();
	root[ "record" ][ "creation_time" ] = creation_time[ 0 ][ 1 ].as< std::size_t >();

	root[ "file" ][ "import_time_human" ] = store_time[ 0 ][ 0 ].as< std::string >();
	root[ "file" ][ "import_time" ] = store_time[ 0 ][ 1 ].as< std::size_t >();

	root[ "file" ][ "deleted_time_human" ] = delete_time[ 0 ][ "cluster_delete_time" ].as< std::string >();
	root[ "file" ][ "deleted_time" ] = delete_time[ 0 ][ "cluster_delete_time_epoch" ].as< std::size_t >();

	const auto response { drogon::HttpResponse::newHttpJsonResponse( root ) };

	co_await tryParseRecordMetadata( record_id, db );

	co_return response;
}

} // namespace idhan::api
