//
// Created by kj16609 on 3/20/25.
//

#include "MetadataModule.hpp"
#include "api/ClusterAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/size.hpp"
#include "filesystem/IOUring.hpp"
#include "fixme.hpp"
#include "hyapi/helpers.hpp"
#include "logging/log.hpp"
#include "metadata/FileMappedData.hpp"
#include "metadata/parseMetadata.hpp"
#include "mime/FileInfo.hpp"

namespace idhan::api
{

drogon::Task< std::expected< RecordID, drogon::HttpResponsePtr > >
	adoptOrphan( FileIOUring io_uring, drogon::orm::DbClientPtr db )
{
	const auto data { co_await io_uring.readAll() };
	const auto sha256 { SHA256::hash( data.data(), data.size() ) };
	const auto record_result { co_await helpers::createRecord( sha256, db ) };

	co_return record_result;
}

drogon::Task< drogon::HttpResponsePtr > ClusterAPI::scan( drogon::HttpRequestPtr request, const ClusterID cluster_id )
{
	// This function will start by scanning the files in the cluster.

	const auto db { drogon::app().getDbClient() };

	const auto result {
		co_await db->execSqlCoro( "SELECT folder_path, read_only FROM file_clusters WHERE cluster_id = $1", cluster_id )
	};

	if ( result.empty() ) co_return createBadRequest( "Cluster was not found with ID {}", cluster_id );

	const auto cluster_path { result[ 0 ][ "folder_path" ].as< std::string >() };
	const bool read_only { result[ 0 ][ "read_only" ].as< bool >() };

	std::size_t size_accum { 0 };
	std::uint32_t file_counter { 0 };

	// Trust the filename, Ignore the hash given by the file
	auto recompute_hash { request->getOptionalParameter< bool >( "recompute_hash" ).value_or( true ) };

	// Scans the mime if the file has not been scanned previously
	auto scan_mime { request->getOptionalParameter< bool >( "scan_mime" ).value_or( true ) };
	// Triggers the mime to be retested for all files
	auto rescan_mime { request->getOptionalParameter< bool >( "rescan_mime" ).value_or( false ) };

	// Uses a metadata parser to scan for metadata
	auto scan_metadata { request->getOptionalParameter< bool >( "scan_metadata" ).value_or( true ) };
	// Forces the metadata parser to run, even if data is already present
	auto rescan_metadata { request->getOptionalParameter< bool >( "rescan_metadata" ).value_or( true ) };

	const auto stop_on_fail { request->getOptionalParameter< bool >( "stop_on_fail" ).value_or( true ) };

	auto adopt_orphans { request->getOptionalParameter< bool >( "adopt_orphans" ).value_or( false ) };

	if ( adopt_orphans )
	{
		scan_metadata = true;
		scan_mime = true;
	}

	if ( scan_metadata )
	{
		scan_mime = true;
	}

	if ( read_only && !recompute_hash )
		co_return createBadRequest( "Must recompute hash for read only folders (recompute hash was set to false)" );

	// Where we put bad files if not in readonly mode
	const std::filesystem::path bad_dir { cluster_path };

	std::size_t processed_count { 0 };

	for ( const auto& dir_entry : std::filesystem::recursive_directory_iterator( cluster_path ) )
	{
		if ( !dir_entry.is_regular_file() ) continue;

		const auto& file_path { dir_entry.path() };

		if ( file_path.extension() == ".thumbnail" ) continue;

		auto data { std::make_shared< FileMappedData >( dir_entry.path() ) };

		FileIOUring io_uring { file_path };

		const auto sha256_e { recompute_hash ? co_await SHA256::hashCoro( io_uring ) :
			                                   SHA256::fromHex( data->name() ) };

		if ( !sha256_e )
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

			if ( !read_only )
			{
				std::filesystem::rename( file_path, bad_dir / file_path.filename() );
				log::warn( "{}; File was moved to {}", error_str, ( bad_dir / file_path.filename() ).string() );
			}
			else
			{
				log::warn( error_str );
				if ( stop_on_fail ) co_return createInternalError( error_str );
			}

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
		const auto record_id_e { search.empty() && adopt_orphans ? co_await adoptOrphan( io_uring, db ) :
			                                                       search[ 0 ][ 0 ].as< RecordID >() };

		if ( !record_id_e ) co_return record_id_e.error();
		const auto record_id { record_id_e.value() };

		bool valid_mime { true };
		if ( scan_mime || rescan_mime )
		{
			log::debug( "Scanning mime for record {}", record_id );
			// Determine if there is a mime for this file
			const auto mime_search {
				co_await db->execSqlCoro( "SELECT mime_id FROM file_info WHERE record_id = $1", record_id )
			};

			if ( rescan_mime || mime_search.empty() || mime_search[ 0 ][ "mime_id" ].isNull() )
			{
				auto info { co_await gatherFileInfo( io_uring, db ) };

				if ( !info )
				{
					if ( stop_on_fail ) co_return info.error();
					continue;
				}

				if ( info->mime_id == constants::INVALID_MIME_ID )
				{
					log::warn(
						"During scan of cluster {} IDHAN came across file {} that it could not determine a mime for: Extension: {}",
						cluster_id,
						data->strpath(),
						data->extension() );
					info->extension = data->extension();
					if ( info->extension.starts_with( '.' ) ) info->extension = info->extension.substr( 1 );
					valid_mime = false;
				}

				log::debug( "Found mime {} for {}", info->mime_id, data->strpath() );

				co_await setFileInfo( record_id, *info, db );

				co_await db
					->execSqlCoro( "UPDATE file_info SET cluster_id = $1 WHERE record_id = $2", cluster_id, record_id );
			}

			//TODO: If the cluster is not readonly, try to fix any invalid extensions
		}

		// if the mime we just processed is not valid then we should skip scanning the metadata as we won't know what parser to use anyways.
		if ( ( scan_metadata || rescan_metadata ) && valid_mime )
		{
			log::debug( "Scanning metadata for record {}", record_id );
			// check if we have metadata already
			const auto metadata_search {
				co_await db->execSqlCoro( "SELECT record_id FROM metadata WHERE record_id = $1", record_id )
			};

			if ( metadata_search.empty() || rescan_metadata )
			{
				const auto parseResult { co_await tryParseRecordMetadata( record_id, db ) };
				if ( !parseResult )
				{
					const auto message { hyapi::helpers::extractIDHANHTTPError( parseResult.error() ) };

					log::warn(
						"During scan of cluster {} file {} with was unable to be parsed for metadata: {}",
						cluster_id,
						data->strpath(),
						message );
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