#include "api/ImportAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "codes/ImportCodes.hpp"
#include "crypto/SHA256.hpp"
#include "db/drogonArrayBind.hpp"
#include "filesystem/clusters/ClusterManager.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/log.hpp"
#include "metadata/metadata.hpp"
#include "mime/MimeDatabase.hpp"
#include "records/records.hpp"

namespace idhan::api
{

Json::Value createDeletedResponse( const RecordID record_id, const int64_t deleted_time )
{
	Json::Value root {};

	root[ "record_id" ] = record_id;
	root[ "cluster_delete_time" ] = deleted_time;
	root[ "status" ] = static_cast< Json::Value::UInt >( Deleted );

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

constexpr auto CLUSTER_TIMESTAMPS_QUERY {
	"SELECT cluster_delete_time, cluster_store_time, "
	"EXTRACT(EPOCH FROM cluster_delete_time)::BIGINT as cluster_delete_time_epoch, "
	"EXTRACT(EPOCH FROM cluster_store_time)::BIGINT AS cluster_store_time_epoch "
	"FROM file_info WHERE record_id = $1 LIMIT 1"
};

Json::Value createImportResponse( const RecordID record_id, const ImportStatus status, const drogon::orm::Row& ts_row )
{
	Json::Value root {};

	root[ "status" ] = static_cast< Json::Value::UInt >( status );
	root[ "record_id" ] = record_id;

	root[ "record" ][ "id" ] = record_id;

	// NULL timestamps must come through as json null, Field::as<T>() would silently
	// turn them into "" and 0 (epoch 1970)
	if ( ts_row[ "cluster_store_time" ].isNull() )
	{
		root[ "file" ][ "import_time_human" ] = Json::Value {};
		root[ "file" ][ "import_time" ] = Json::Value {};
	}
	else
	{
		root[ "file" ][ "import_time_human" ] = ts_row[ "cluster_store_time" ].as< std::string >();
		root[ "file" ][ "import_time" ] = ts_row[ "cluster_store_time_epoch" ].as< int64_t >();
	}

	if ( ts_row[ "cluster_delete_time" ].isNull() )
	{
		root[ "file" ][ "deleted_time_human" ] = Json::Value {};
		root[ "file" ][ "deleted_time" ] = Json::Value {};
	}
	else
	{
		root[ "file" ][ "deleted_time_human" ] = ts_row[ "cluster_delete_time" ].as< std::string >();
		root[ "file" ][ "deleted_time" ] = ts_row[ "cluster_delete_time_epoch" ].as< int64_t >();
	}

	return root;
}

drogon::Task< drogon::HttpResponsePtr > ImportAPI::importFile( const drogon::HttpRequestPtr request )
{
	FGL_ASSERT( request, "Request invalid" );
	const auto request_data { request->getBody() };
	[[maybe_unused]] const auto content_type { request->getContentType() };

	if ( request_data.empty() ) co_return createBadRequest( "No file data supplied in the request body" );

	auto db { drogon::app().getDbClient() };

	const std::byte* data_ptr { reinterpret_cast< const std::byte* >( request_data.data() ) };
	const auto data_length { request_data.size() };

	const SHA256 sha256 { SHA256::hash( data_ptr, data_length ) };

	const bool force_import { request->getOptionalParameter< bool >( "force_import" ).value_or( false ) };

	// Everything below this needs the mime, which means sniffing the body and then parsing metadata
	// out of the stored file. A hash we already hold needs none of it, so answer from the hash alone.
	if ( !force_import )
	{
		if ( const auto existing { co_await helpers::findRecord( sha256, db ) } )
		{
			const auto timestamps { co_await db->execSqlCoro( CLUSTER_TIMESTAMPS_QUERY, *existing ) };

			if ( !timestamps.empty() )
			{
				const auto& row { timestamps[ 0 ] };

				if ( !row[ "cluster_delete_time" ].isNull() )
					co_return drogon::HttpResponse::newHttpJsonResponse(
						createDeletedResponse( *existing, row[ "cluster_delete_time_epoch" ].as< int64_t >() ) );

				// Only bail when the bytes are actually still there; a record whose file went missing
				// falls through so the normal path can store it again.
				const auto filepath { co_await filesystem::getRecordPath( *existing, db ) };

				if ( filepath && std::filesystem::exists( *filepath ) )
					co_return drogon::HttpResponse::newHttpJsonResponse(
						createImportResponse( *existing, ImportStatus::Exists, row ) );
			}
		}
	}

	//TODO: Add multipart for getting the file name
	const auto mime_str { co_await mime::getMimeDatabase()->scan( request_data, "" ) };

	std::string mime_name {};

	if ( mime_str )
		mime_name = mime_str.value();
	else if ( force_import )
		// the file could not be identified, force imports store it under the unknown mime
		mime_name = INVALID_MIME_NAME;
	else
	{
		// If the mime type is not known, Then simply skip it.
		co_return drogon::HttpResponse::newHttpJsonResponse( createUnknownMimeResponse() );
	}

	const bool is_octet { mime_name == INVALID_MIME_NAME };

	if ( is_octet && !force_import )
	{
		co_return createBadRequest(
			"Mime type not known by IDHAN. Either set the force import flag in the parameters, "
			"or teach IDHAN how to detect the mime for this file" );
	}

	const auto mime_id { co_await mime::getMimeIDFromStr( mime_name, db ) };

	if ( !mime_id ) co_return mime_id.error();

	const auto record_id_e { co_await helpers::createRecord( sha256, db ) };

	if ( !record_id_e ) co_return record_id_e.error();

	const auto record_id { record_id_e.value() };

	// A file_info row must either name a cluster or carry a delete time (cluster_id_xor_delete_time),
	// so the target cluster has to be chosen before the row can be created. The store time is only
	// set once the bytes actually land in the cluster.
	const auto target_cluster {
		co_await filesystem::ClusterManager::getInstance().findBestFolder( record_id, data_length, db )
	};

	if ( !target_cluster ) co_return target_cluster.error();

	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, mime_id, size, cluster_id, modified_time) VALUES ($1, $2, $3, $4, now()) ON CONFLICT DO NOTHING",
		record_id,
		*mime_id,
		data_length,
		*target_cluster );

	// select deleted time and store time
	const auto cluster_timestamps { co_await db->execSqlCoro( CLUSTER_TIMESTAMPS_QUERY, record_id ) };

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
			record_id, cluster_timestamps[ 0 ][ "cluster_delete_time_epoch" ].as< int64_t >() ) );
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

	// re-read the timestamps, a store that just happened updated cluster_store_time
	const auto final_timestamps { co_await db->execSqlCoro( CLUSTER_TIMESTAMPS_QUERY, record_id ) };

	// Keyed on whether the file was on disk before this request, not on store_recorded: a row can
	// carry a store time while the file is gone from the cluster, and storeFile is also allowed to
	// succeed without writing when the record is held in a read-only cluster.
	const auto response { drogon::HttpResponse::newHttpJsonResponse( createImportResponse(
		record_id, store_confirmed ? ImportStatus::Exists : ImportStatus::Success, final_timestamps[ 0 ] ) ) };

	// a metadata failure should not fail the import, the file itself was stored fine
	if ( const auto parse_result { co_await metadata::tryParseRecordMetadata( record_id, db ) }; !parse_result )
		log::warn( "importFile: failed to parse metadata for record {}", record_id );

	co_return response;
}

} // namespace idhan::api
