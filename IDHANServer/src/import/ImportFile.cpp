#include "ImportFile.hpp"

#include <filesystem>

#include "MimeIDs.hpp"
#include "crypto/SHA256.hpp"
#include "filesystem/clusters/ClusterManager.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/log.hpp"
#include "metadata/metadata.hpp"
#include "mime/identifyMime.hpp"
#include "records/records.hpp"

namespace idhan::imports
{
constexpr auto IMPORT_FILE_CLUSTER_TIMESTAMPS_QUERY {
	"SELECT cluster_delete_time, cluster_store_time, "
	"EXTRACT(EPOCH FROM cluster_delete_time)::BIGINT AS cluster_delete_time_epoch "
	"FROM file_info WHERE record_id = $1 LIMIT 1"
};

ExpectedTask< ImportFileResult > importFile(
	const std::span< const std::byte > data,
	std::string filename,
	const bool force_import,
	DbClientPtr db )
{
	const SHA256 sha256 { SHA256::hash( data.data(), data.size() ) };

	if ( !force_import )
	{
		if ( const auto existing { co_await helpers::findRecord( sha256, db ) } )
		{
			const auto timestamps { co_await db->execSqlCoro( IMPORT_FILE_CLUSTER_TIMESTAMPS_QUERY, *existing ) };

			if ( !timestamps.empty() )
			{
				const auto& row { timestamps[ 0 ] };

				if ( !row[ "cluster_delete_time" ].isNull() )
					co_return ImportFileResult {
						.record_id = *existing,
						.status = ImportStatus::Deleted,
						.deleted_at = row[ "cluster_delete_time_epoch" ].as< std::int64_t >(),
					};

				const auto filepath { co_await filesystem::getRecordPath( *existing, db ) };

				if ( filepath ) co_return ImportFileResult { .record_id = *existing, .status = ImportStatus::Exists };
			}
		}
	}

	const auto mime_id { co_await mime::identifyMime( data, filename ) };

	if ( mime_id == mime_ids::UNKNOWN && !force_import ) co_return ImportFileResult {};

	const auto record_id { co_await helpers::createRecord( sha256, db ) };
	return_unexpected_error( record_id );

	const auto target_cluster {
		co_await filesystem::ClusterManager::getInstance().findBestFolder( *record_id, data.size(), db )
	};
	return_unexpected_error( target_cluster );

	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, mime_id, size, cluster_id, file_mtime) "
		"VALUES ($1, $2, $3, $4, now()) ON CONFLICT DO NOTHING",
		*record_id,
		mime_id,
		data.size(),
		*target_cluster );

	const auto timestamps { co_await db->execSqlCoro( IMPORT_FILE_CLUSTER_TIMESTAMPS_QUERY, *record_id ) };
	const bool delete_recorded { !timestamps[ 0 ][ "cluster_delete_time" ].isNull() };
	const bool store_recorded { !timestamps[ 0 ][ "cluster_store_time" ].isNull() };

	if ( delete_recorded && !force_import )
		co_return ImportFileResult {
			.record_id = *record_id,
			.status = ImportStatus::Deleted,
			.deleted_at = timestamps[ 0 ][ "cluster_delete_time_epoch" ].as< std::int64_t >(),
		};

	const auto filepath { co_await filesystem::getUncheckedRecordPath( *record_id, db ) };
	const bool store_confirmed { filepath && std::filesystem::exists( *filepath ) };
	const bool should_store {
		( !delete_recorded && !store_recorded ) || force_import || ( !store_confirmed && store_recorded )
	};

	if ( should_store )
	{
		const auto stored {
			co_await filesystem::ClusterManager::getInstance().storeFile( *record_id, data.data(), data.size(), db )
		};
		return_unexpected_error( stored );
	}

	if ( const auto parsed { co_await metadata::parseAndUpdateRecordMetadata( *record_id, db ) }; !parsed )
		log::warn( "importFile: failed to parse metadata for record {}", *record_id );

	co_return ImportFileResult {
		.record_id = *record_id,
		.status = store_confirmed && !force_import ? ImportStatus::Exists : ImportStatus::Success,
	};
}
} // namespace idhan::imports
