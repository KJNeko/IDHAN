//
// Created by kj16609 on 3/20/25.
//

#include "MetadataModule.hpp"
#include "api/ClusterAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/size.hpp"
#include "logging/log.hpp"
#include "metadata/FileMappedData.hpp"
#include "metadata/parseMetadata.hpp"
#include "mime/FileInfo.hpp"

namespace idhan::api
{

drogon::Task< std::expected< RecordID, drogon::HttpResponsePtr > >
	adoptOrphan( std::shared_ptr< FileMappedData > data, drogon::orm::DbClientPtr db )
{
	const auto sha256 { SHA256::hash( data->data(), data->length() ) };
	const auto record_result { co_await helpers::createRecord( sha256, db ) };

	co_return record_result;
}

drogon::Task< drogon::HttpResponsePtr > ClusterAPI::scan( drogon::HttpRequestPtr request, const ClusterID cluster_id )
{
	// This function will start by scanning the files in the cluster.

	const auto db { drogon::app().getDbClient() };

	const auto result {
		co_await db->execSqlCoro( "SELECT folder_path FROM file_clusters WHERE cluster_id = $1", cluster_id )
	};

	if ( result.empty() ) co_return createBadRequest( "Cluster not found with ID" );

	const auto path_str { result[ 0 ][ 0 ].as< std::string >() };

	std::size_t size_accum { 0 };
	std::uint32_t file_counter { 0 };

	// Trust the filename, Ignore the hash given by the file
	const auto recompute_hash { request->getOptionalParameter< bool >( "recompute_hash" ).value_or( true ) };

	// Scans the mime if the file has not been scanned previously
	const auto scan_mime { request->getOptionalParameter< bool >( "scan_mime" ).value_or( true ) };
	// Triggers the mime to be retested for all files
	const auto rescan_mime { request->getOptionalParameter< bool >( "rescan_mime" ).value_or( false ) };

	// Uses a metadata parser to scan for metadata
	const auto scan_metadata { request->getOptionalParameter< bool >( "scan_metadata" ).value_or( true ) };
	// Forces the metadata parser to run, even if data is already present
	const auto rescan_metadata { request->getOptionalParameter< bool >( "rescan_metadata" ).value_or( true ) };

	const auto stop_on_fail { request->getOptionalParameter< bool >( "stop_on_fail" ).value_or( true ) };

	const auto adopt_orphans { request->getOptionalParameter< bool >( "adopt_orphans" ).value_or( false ) };

	for ( const auto& dir_entry : std::filesystem::recursive_directory_iterator( path_str ) )
	{
		if ( !dir_entry.is_regular_file() ) continue;

		auto data { std::make_shared< FileMappedData >( dir_entry.path() ) };

		if ( data->extension() == ".thumbnail" ) continue;

		const auto sha256_e { recompute_hash ? SHA256::hash( data->data(), data->length() ) :
			                                   SHA256::fromHex( data->name() ) };

		if ( !sha256_e.has_value() )
		{
			if ( stop_on_fail ) co_return createInternalError( "Failed to get hash for file" );
			log::warn( "Failed to get hash for file {}", data->strpath() );
			continue;
		}

		const auto& sha256 { sha256_e.value() };

		// We are expecting that the filename without the extension should be the file
		const auto expected_hex { sha256.hex() };

		if ( expected_hex != data->name() )
		{
			const auto error_str { format_ns::format(
				"While scanning cluster {} file {} was found to have name of {} but hash of {}",
				cluster_id,
				data->strpath(),
				data->name(),
				expected_hex ) };

			if ( stop_on_fail ) co_return createInternalError( error_str );
			log::warn( error_str );

			//TODO: If not readonly, Move this file to a new location for processing by the user
			continue;
		}

		// Check if this file is contained within the DB
		const auto search {
			co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", sha256.toVec() )
		};

		// Orphan check
		if ( search.empty() && !adopt_orphans )
		{
			const auto error_str { format_ns::format(
				"During scan of cluster {} file {} was found to have hash {} but was not in the DB. Possible orphan file",
				cluster_id,
				data->strpath(),
				data->name() ) };

			if ( stop_on_fail ) co_return createInternalError( error_str );
			log::warn( error_str );
			continue;
		}

		// if we didn't find a record and we are set to adopt any orphans we find, then we'll adopt it here.
		const auto record_id_e { search.empty() && adopt_orphans ? co_await adoptOrphan( data, db ) :
			                                                       search[ 0 ][ 0 ].as< RecordID >() };

		if ( !record_id_e.has_value() ) co_return record_id_e.error();
		const auto record_id { record_id_e.value() };

		bool valid_mime { true };
		if ( scan_mime || rescan_mime )
		{
			// Determine if there is a mime for this file
			const auto mime_search {
				co_await db->execSqlCoro( "SELECT mime_id FROM file_info WHERE record_id = $1", record_id )
			};

			if ( mime_search.empty() || rescan_mime )
			{
				auto info { co_await gatherFileInfo( data, db ) };

				if ( info.mime_id == constants::INVALID_MIME_ID )
				{
					log::warn(
						"IDHAN came across file {} that it could not determine a mime for: Extension: {}",
						data->strpath(),
						data->extension() );
					info.extension = data->extension();
					if ( info.extension.starts_with( '.' ) ) info.extension = info.extension.substr( 1 );
					valid_mime = false;
				}

				co_await setFileInfo( record_id, info, db );

				co_await db
					->execSqlCoro( "UPDATE file_info SET cluster_id = $1 WHERE record_id = $2", cluster_id, record_id );
			}

			//TODO: If the cluster is not readonly, try to fix any invalid extensions
		}

		// if the mime we just processed is not valid then we should skip scanning the metadata as we won't know what parser to use anyways.
		if ( ( scan_metadata || rescan_metadata ) && valid_mime )
		{
			// check if we have metadata already
			const auto metadata_search {
				co_await db->execSqlCoro( "SELECT record_id FROM metadata WHERE record_id = $1", record_id )
			};

			if ( metadata_search.empty() || rescan_metadata )
			{
				const auto metadata { co_await getMetadata( record_id, data, db ) };

				if ( metadata.has_value() )
				{
					co_await updateRecordMetadata( record_id, db, metadata.value() );
				}
				else
				{
					const auto json { metadata.error()->getJsonObject() };

					if ( !json ) co_return createInternalError( "Metadata returned invalid response internally" );

					log::warn(
						"During scan of cluster {} file {} with was unable to be parsed for metadata: {}",
						cluster_id,
						data->strpath(),
						( *json )[ "error" ].asString() );
				}
			}
		}

		// The file is ours and has passed all verification
		size_accum += data->length();
		file_counter += 1;
	}

	log::info(
		"Finished scanning {} with size of {} and file count of {}",
		cluster_id,
		fgl::size::toHuman( size_accum ),
		file_counter );

	co_await db->execSqlCoro(
		"UPDATE file_clusters SET size_used = $1, file_count = $3 WHERE cluster_id = $2",
		size_accum,
		cluster_id,
		file_counter );

	request->setPath( format_ns::format( "/clusters/{}/info", cluster_id ) );

	co_return co_await drogon::app().forwardCoro( request );
}
} // namespace idhan::api