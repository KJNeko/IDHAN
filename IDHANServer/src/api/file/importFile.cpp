//
// Created by kj16609 on 11/15/24.
//

#include "../../filesystem/clusters/ClusterManager.hpp"
#include "../../records/records.hpp"
#include "api/ImportAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "codes/ImportCodes.hpp"
#include "crypto/SHA256.hpp"
#include "db/drogonArrayBind.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/log.hpp"
#include "metadata/metadata.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan::api
{

Json::Value createDeletedResponse( const RecordID record_id, const std::size_t deleted_time )
{
	Json::Value root {};

	root[ "record_id" ] = record_id;
	root[ "cluster_delete_time" ] = deleted_time;
	root[ "status" ] = static_cast< Json::Value::UInt >( Deleted );

	return root;
}

Json::Value createAlreadyImportedResponse( const RecordID record_id, const std::size_t import_time )
{
	Json::Value root {};

	root[ "record_id" ] = record_id;
	root[ "cluster_store_time" ] = import_time;
	root[ "status" ] = static_cast< Json::Value::UInt >( Exists );

	return root;
}

Json::Value createUnknownMimeResponse()
{
	Json::Value root {};

	root[ "reason" ] = "unknown mime";
	root[ "reason_id" ] = static_cast< Json::Value::UInt >( UnknownMime );
	root[ "status" ] = static_cast< Json::Value::UInt >( Failed );

	return root;
}

drogon::Task< drogon::HttpResponsePtr > ImportAPI::importFile( const drogon::HttpRequestPtr request )
{
	FGL_ASSERT( request, "Request invalid" );
	const auto request_data { request->getBody() };
	[[maybe_unused]] const auto content_type { request->getContentType() };

	auto db { drogon::app().getDbClient() };

	const std::byte* data_ptr { reinterpret_cast< const std::byte* >( request_data.data() ) };
	const auto data_length { request_data.size() };

	const SHA256 sha256 { SHA256::hash( data_ptr, data_length ) };

	//TODO: Add multipart for getting the file name
	const auto mime_str { co_await mime::getMimeDatabase()->scan( request_data, "" ) };

	const bool force_import { request->getOptionalParameter< bool >( "force_import" ).value_or( false ) };

	if ( !mime_str )
	{
		// If the mime type is not known, Then simply skip it.
		co_return drogon::HttpResponse::newHttpJsonResponse( createUnknownMimeResponse() );
	}

	const bool is_octet { mime_str == INVALID_MIME_NAME };

	if ( is_octet && !force_import )
	{
		co_return createBadRequest(
			"Mime type not known by IDHAN. Either set the force import flag in the parameters, "
			"or teach IDHAN how to detect the mime for this file" );
	}

	const auto mime_id { co_await mime::getMimeIDFromStr( mime_str.value(), db ) };

	if ( !mime_id ) co_return mime_id.error();

	const auto record_id_e { co_await helpers::createRecord( sha256, db ) };

	if ( !record_id_e ) co_return record_id_e.error();

	const auto record_id { record_id_e.value() };

	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, mime_id, size, cluster_store_time, modified_time) VALUES ($1, $2, $3, now(), now()) ON CONFLICT DO NOTHING",
		record_id,
		*mime_id,
		data_length );

	// select deleted time and store time
	const auto cluster_timestamps { co_await db->execSqlCoro(
		"SELECT cluster_delete_time, cluster_store_time, "
		"EXTRACT(EPOCH FROM cluster_delete_time)::BIGINT as cluster_delete_time_epoch, "
		"EXTRACT(EPOCH FROM cluster_store_time)::BIGINT AS cluster_store_time_epoch "
		"FROM file_info WHERE record_id = $1 LIMIT 1",
		record_id ) };

	//! True if there is a delete recorded
	const bool delete_recorded { !cluster_timestamps[ 0 ][ "cluster_delete_time" ].isNull() };
	//! True if there has been a store recorded
	const bool store_recorded { !cluster_timestamps[ 0 ][ "cluster_store_time" ].isNull() };
	// if the file is not deleted, it is stored, But if the overwrite flag is on. Store it anyway
	// T (store time is not null) && F (overwrite flag is true) // Not stored

	if ( delete_recorded && !force_import )
	{
		// file was deleted, we can simply return now.
		co_return drogon::HttpResponse::newHttpJsonResponse( createDeletedResponse(
			record_id, cluster_timestamps[ 0 ][ "cluster_delete_time_epoch" ].as< std::size_t >() ) );
	}

	//! True if the file has been confirmed to be stored still
	const auto filepath { co_await filesystem::getRecordPath( record_id, db ) };
	const bool store_confirmed { filepath ? std::filesystem::exists( *filepath ) : false };

	// If there is no delete recorded & no store recorded, store it
	// If force import is true, then store it anyway
	// if there is a store, but no delete request, and the file is not present. store it again
	const bool should_store {
		( !delete_recorded && !store_recorded ) || force_import || ( !store_confirmed && store_recorded )
	};

	if ( should_store )
	{
		const auto store_result {
			co_await filesystem::ClusterManager::getInstance().storeFile( record_id, data_ptr, data_length, db )
		};

		if ( !store_result )
		{
			co_return store_result.error();
		}
	}

	Json::Value root {};

	root[ "status" ] =
		static_cast< Json::Value::UInt >( store_recorded ? ImportStatus::Exists : ImportStatus::Success );
	root[ "record_id" ] = record_id;

	root[ "record" ][ "id" ] = record_id;

	root[ "file" ][ "import_time_human" ] = cluster_timestamps[ 0 ][ "cluster_store_time" ].as< std::string >();
	root[ "file" ][ "import_time" ] = cluster_timestamps[ 0 ][ "cluster_store_time_epoch" ].as< std::size_t >();

	root[ "file" ][ "deleted_time_human" ] = cluster_timestamps[ 0 ][ "cluster_delete_time" ].as< std::string >();
	root[ "file" ][ "deleted_time" ] = cluster_timestamps[ 0 ][ "cluster_delete_time_epoch" ].as< std::size_t >();

	const auto response { drogon::HttpResponse::newHttpJsonResponse( root ) };

	co_await metadata::tryParseRecordMetadata( record_id, db );

	co_return response;
}

} // namespace idhan::api
