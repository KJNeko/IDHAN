#pragma once
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "io/IOUring.hpp"
#include "ipc/Blob.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan
{
class SHA256;
}

namespace idhan::modules
{
class CallInput;
}

namespace idhan::filesystem
{

[[nodiscard]] std::filesystem::path getFileFolder( const SHA256& sha256 );

ExpectedTask< std::filesystem::path > getRecordPath( RecordID record_id, DbClientPtr db );

ExpectedTask< FileIOUring > getIOForRecord( RecordID record_id, DbClientPtr db );

//! Prepares a record's file as the input to a module call.
ExpectedTask< std::shared_ptr< const modules::CallInput > > openRecordInput( RecordID record_id, DbClientPtr db );

//! Empties the on-disk thumbnail cache (`[thumbnails] path`) and recreates the directory.
/** Cache-only state: fetchThumbnail decides what to serve purely by whether the file is there, so a
 *  purged entry regenerates on the next request. Thumbnails stored inside clusters are a different
 *  thing entirely and are not touched.
 *
 *  \return How many files were removed, or why the purge failed. A missing directory is not a
 *          failure. Reports errors rather than throwing, because both callers (the maintenance
 *          endpoint and startup) have to survive a failed purge. */
[[nodiscard]] std::expected< std::size_t, std::string > clearThumbnailCache();

ExpectedTask< std::filesystem::path > getClusterPath( ClusterID cluster_id );

//! The path a file with this hash would occupy in this cluster, whether or not it is there.
ExpectedTask< std::filesystem::path > getTheoreticalFilePath(
	ClusterID cluster_id,
	SHA256 sha256,
	std::string extension );

//! Checks that a file exists in its respective cluster.
ExpectedTask< bool > checkFileExists( RecordID record_id, drogon::orm::DbClientPtr db );

enum class FileState
{
	FileNotFound,
	//! Present, but its contents do not hash to the record's hash.
	FileInvalidHash,
	FileValid
};

//! Checks that a record has a file and that it is valid.
ExpectedTask< FileState > validateFile( RecordID record_id );

//! \return The mtime of the file, in microseconds.
[[nodiscard]] std::int64_t getLastWriteTime( const std::filesystem::path& path );

} // namespace idhan::filesystem
