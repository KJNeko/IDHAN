#pragma once
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

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

//! Canonical path below a cluster root for a stored record.
//! `extension` may be empty or may include its leading dot.
[[nodiscard]] std::filesystem::path getClusterRelativePath( const SHA256& sha256, std::string_view extension );

//! Canonical path recorded for a record, without requiring a file to exist there.
ExpectedTask< std::filesystem::path > getUncheckedRecordPath( RecordID record_id, DbClientPtr db );

ExpectedTask< std::filesystem::path > getTheoreticalFilePath(
	ClusterID cluster_id,
	SHA256 sha256,
	std::string extension );

//! Canonical path for a stored record, guaranteed to be a regular file when returned.
ExpectedTask< std::filesystem::path > getRecordPath( RecordID record_id, DbClientPtr db );

//! Central reporting point for an expected stored file that is absent.
Task<> reportMissingFile( RecordID record_id, const std::filesystem::path& expected_path, DbClientPtr db );

ExpectedTask< FileIOUring > getIOForRecord( RecordID record_id, DbClientPtr db );

ExpectedTask< std::shared_ptr< const modules::CallInput > > openRecordInput( RecordID record_id, DbClientPtr db );

//! Clears only the thumbnail cache directory; missing directories are not failures.
[[nodiscard]] std::expected< std::size_t, std::string > clearThumbnailCache();

ExpectedTask< std::filesystem::path > getClusterPath( ClusterID cluster_id );

ExpectedTask< bool > checkFileExists( RecordID record_id, drogon::orm::DbClientPtr db );

enum class FileState
{
	FileNotFound,
	//! Present, but its contents do not hash to the record's hash.
	FileInvalidHash,
	FileValid
};

ExpectedTask< FileState > validateFile( RecordID record_id, drogon::orm::DbClientPtr db );

[[nodiscard]] std::int64_t getLastWriteTime( const std::filesystem::path& path );

} // namespace idhan::filesystem
