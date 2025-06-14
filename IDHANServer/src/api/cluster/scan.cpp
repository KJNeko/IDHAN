//
// Created by kj16609 on 3/20/25.
//

#include "MetadataModule.hpp"
#include "api/ClusterAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/size.hpp"
#include "logging/log.hpp"
#include "metadata/FileMappedData.hpp"
#include "metadata/parseMetadata.hpp"
#include "mime/FileInfo.hpp"

namespace idhan::api
{

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

	/*
	constexpr std::size_t in_flight { 4 };

	std::counting_semaphore< in_flight > in_counter { in_flight };
	std::counting_semaphore< in_flight > out_counter { 0 };

	std::thread feeder {};
	std::thread w0, w1, w2, w3;
	*/

	// Trust the filename, Ignore the hash given by the file
	const auto trust_filename { request->getOptionalParameter< bool >( "trust_filename" ).value_or( false ) };

	// Scans the mime if the file has not been scanned previously
	const auto scan_mime { request->getOptionalParameter< bool >( "scan_mime" ).value_or( true ) };

	// Triggers the mime to be retested for all files
	const auto rescan_mime { request->getOptionalParameter< bool >( "rescan_mime" ).value_or( false ) };

	const auto scan_metadata { request->getOptionalParameter< bool >( "scan_metadata" ).value_or( false ) };

	for ( const auto& dir_entry : std::filesystem::recursive_directory_iterator( path_str ) )
	{
		if ( !dir_entry.is_regular_file() ) continue;

		auto data { std::make_shared< FileMappedData >( dir_entry.path() ) };

		if ( data->extension() == ".thumbnail" ) continue;

		const auto sha256_e { trust_filename ? SHA256::fromHex( data->name() ) :
			                                   SHA256::hash( data->data(), data->length() ) };

		if ( !sha256_e.has_value() )
		{
			log::warn( "Failed to get hash for file {}", data->strpath() );
			continue;
		}

		const auto& sha256 { sha256_e.value() };

		// We are expecting that the filename without the extension should be the file
		const auto expected_hex { sha256.hex() };

		if ( expected_hex != data->name() )
		{
			log::warn(
				"During scan of cluster {} file {} was found to have the name of {} but hash of {}",
				cluster_id,
				data->strpath(),
				data->name(),
				expected_hex );

			//TODO: If not readonly, Move this file to a new location for processing by the user
			continue;
		}

		// Check if this file is contained within the DB
		const auto search {
			co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", sha256.toVec() )
		};

		// Orphan check
		if ( search.empty() )
		{
			log::warn(
				"During scan of cluster {} file {} was found to have hash {} but was not in the DB. Possible orphan file",
				cluster_id,
				data->strpath(),
				data->name() );
			continue;
		}

		const RecordID record_id { search[ 0 ][ 0 ].as< RecordID >() };

		if ( scan_mime )
		{
			// Determine if there is a mime for this file
			const auto mime_search {
				co_await db->execSqlCoro( "SELECT mime_id FROM file_info WHERE record_id = $1", record_id )
			};

			if ( rescan_mime || mime_search.empty() )
			{
				auto info { co_await gatherFileInfo( data, db ) };

				if ( info.mime_id == constants::INVALID_MIME_ID )
				{
					log::warn(
						"IDHAN came across file {} that it could not determine a mime for: Extension: {}",
						data->strpath(),
						data->extension() );
					info.extension = data->extension();
				}

				co_await setFileInfo( record_id, info, db );

				co_await db
					->execSqlCoro( "UPDATE file_info SET cluster_id = $1 WHERE record_id = $2", cluster_id, record_id );
			}
		}

		if ( scan_metadata )
		{
			const auto metadata { co_await getMetadata( record_id, data, db ) };

			if ( metadata.has_value() )
			{
				co_await updateRecordMetadata( record_id, db, metadata.value() );
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

	co_return drogon::HttpResponse::newHttpResponse();
}
} // namespace idhan::api