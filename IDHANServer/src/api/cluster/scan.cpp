//
// Created by kj16609 on 3/20/25.
//

#include <sys/mman.h>

#include <fcntl.h>
#include <filesystem>

#include "api/ClusterAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/size.hpp"
#include "logging/log.hpp"
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

	struct Data
	{
		std::byte* data;
		std::size_t length;
		std::filesystem::path path;

		std::string extension() { return path.extension().string(); }

		std::string name() { return path.stem().string(); }

		~Data()
		{
			if ( data ) munmap( data, length );
		}
	};

	const auto scan_hashes { request->getOptionalParameter< bool >( "scan_hashes" ).value_or( true ) };
	const auto scan_mime { request->getOptionalParameter< bool >( "scan_mime" ).value_or( true ) };

	for ( const auto& dir_entry : std::filesystem::recursive_directory_iterator( path_str ) )
	{
		if ( !dir_entry.is_regular_file() ) continue;

		Data data {};

		data.path = dir_entry.path();

		if ( data.extension() == ".thumbnail" ) continue;

		if ( scan_hashes || scan_mime )
		{
			int fd { open( data.path.c_str(), O_RDONLY ) };
			data.length = std::filesystem::file_size( data.path );

			data.data = static_cast< std::byte* >( mmap( nullptr, data.length, PROT_READ, MAP_SHARED, fd, 0 ) );

			close( fd );
		}

		const auto sha256_e { scan_hashes ? SHA256::hash( data.data, data.length ) : SHA256::fromHex( data.name() ) };

		if ( !sha256_e.has_value() )
		{
			log::warn( "Failed to get hash for file {}", data.path.string() );
			continue;
		}

		const auto& sha256 { sha256_e.value() };

		// We are expecting that the filename without the extension should be the file
		const auto expected_hex { sha256.hex() };

		if ( scan_hashes && expected_hex != data.name() )
		{
			log::warn(
				"During scan of cluster {} file {} was found to have the name of {} but hash of {}",
				cluster_id,
				data.path.string(),
				data.name(),
				expected_hex );

			//TODO: If not readonly delete the file, or move it to a new location
			continue;
		}

		// Check if this file is contained within the DB
		const auto search {
			co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", sha256.toVec() )
		};

		if ( search.empty() )
		{
			log::warn(
				"During scan of cluster {} file {} was found to have hash {} but was not in the DB. Possible orphan file",
				cluster_id,
				data.path.string(),
				data.name() );
			continue;
		}

		const RecordID record_id { search[ 0 ][ 0 ].as< RecordID >() };

		if ( scan_mime )
		{
			// Determine if there is a mime for this file
			const auto mime_search {
				co_await db->execSqlCoro( "SELECT mime_id FROM file_info WHERE record_id = $1", record_id )
			};

			if ( mime_search.empty() )
			{
				auto info { co_await gatherFileInfo( data.data, data.length, db ) };

				if ( info.mime_id == 0 )
				{
					log::warn(
						"IDHAN came across file {} that it could not determine a mime for: Extension: {}",
						data.path.string(),
						data.extension() );
					info.extension = data.extension();
				}

				co_await setFileInfo( record_id, info, db );

				co_await db
					->execSqlCoro( "UPDATE file_info SET cluster_id = $1 WHERE record_id = $2", cluster_id, record_id );
			}
		}

		// The file is ours and has passed all verification
		size_accum += data.length;
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