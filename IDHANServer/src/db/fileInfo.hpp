#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "filesystem/PathHelpers.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;

struct RecordFileState
{
	RecordID record_id;
	std::optional< ClusterID > cluster_id;
	std::optional< MimeID > mime_id;
	std::optional< std::string > extension;
	std::optional< std::string > best_extension;
	std::optional< std::int64_t > size;
	std::optional< std::int64_t > mtime_us;
	std::optional< std::int64_t > btime_us;
	bool stored;
	bool has_metadata;
};

struct ClusterRecordFile
{
	RecordID record_id;
	SHA256 hash;
	std::string extension;
};

[[nodiscard]] runtime::Task< std::optional< RecordFileState > > findRecordFileState(
	DBContext& db,
	const SHA256& hash );

// The normalized URL must be classified as 'file' and map to exactly one stored record.
[[nodiscard]] runtime::Task< std::optional< RecordID > > findStoredDirectRecordByUrl( DBContext& db, std::string url );

// Takes a normalized URL. Includes page and source URLs; excludes records whose files are no longer stored.
[[nodiscard]] runtime::Task< std::vector< SHA256 > > storedRecordHashesForUrl( DBContext& db, std::string url );

[[nodiscard]] runtime::Task< void > markDirectUrls( DBContext& db, std::vector< std::string > urls );

[[nodiscard]] runtime::Task< std::optional< RecordID > > findMissingRecordForHash( DBContext& db, const SHA256& hash );

[[nodiscard]] runtime::Task< bool > isRecordFileMissing( DBContext& db, RecordID record_id );

[[nodiscard]] runtime::Task< std::optional< std::string > > bestMimeExtension( DBContext& db, MimeID mime_id );

[[nodiscard]] runtime::Task< void > storeRecordFileInfo(
	DBContext& db,
	RecordID record_id,
	ClusterID cluster_id,
	std::int64_t size,
	std::optional< MimeID > mime_id,
	std::string extension,
	filesystem::FileTimes times );

// Keyset pagination bounds preflight memory without keeping a database connection leased during I/O.
[[nodiscard]] runtime::Task< std::vector< ClusterRecordFile > > clusterRecordFiles(
	DBContext& db,
	ClusterID cluster_id,
	RecordID after_record_id,
	std::int32_t limit );

[[nodiscard]] runtime::Task< void > detachMissingRecordFile( DBContext& db, RecordID record_id, ClusterID cluster_id );
} // namespace idhan::db
