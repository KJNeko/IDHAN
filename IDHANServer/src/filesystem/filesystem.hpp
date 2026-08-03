//
// Created by kj16609 on 11/13/25.
//
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

/**
 *
 * @param record_id Record of which to get a filepath for
 * @param db
 * @return
 */
ExpectedTask< std::filesystem::path > getRecordPath( RecordID record_id, DbClientPtr db );

//! Returns a FileIOUring instance for the given record
ExpectedTask< FileIOUring > getIOForRecord( RecordID record_id, DbClientPtr db );

//! Prepares a record's file as the input to a module call.
/** Whether the bytes reach the worker through a restricted io_uring or through a copy in anonymous
 *  memory is CallInput's decision, not the call site's -- which is why call sites ask for this
 *  rather than for a blob.
 *
 *  The input must outlive any module call made with it. It is shared rather than owned because the
 *  worker registry also holds it: a module can pass its own input back through a callback, and
 *  answering that by reference is what keeps a large file from being shipped twice. */
ExpectedTask< std::shared_ptr< const modules::CallInput > > openRecordInput( RecordID record_id, DbClientPtr db );

//! Empties the on-disk thumbnail cache (`[thumbnails] path`) and recreates the directory.
/** Cache-only state: fetchThumbnail decides what to serve purely by whether the file is there, with
 *  no database rows behind it, so a purged entry simply regenerates on the next request. Thumbnails
 *  stored inside clusters are a different thing entirely and are not touched.
 *
 *  \return How many files were removed, or why the purge failed. A missing directory is not a
 *          failure -- there was nothing to delete. Reports errors rather than throwing, because both
 *          callers (the maintenance endpoint and startup) have to survive a failed purge. */
[[nodiscard]] std::expected< std::size_t, std::string > clearThumbnailCache();

//! Returns the path of a cluster.
ExpectedTask< std::filesystem::path > getClusterPath( ClusterID cluster_id );

//! Returns the path that would occur at a given cluster with a given hash
ExpectedTask< std::filesystem::path > getTheoreticalFilePath(
	ClusterID cluster_id,
	SHA256 sha256,
	std::string extension );

/**
 * @brief Checks that a file exists in its respective cluster
 * @param record_id
 * @param db
 * @return
 */
ExpectedTask< bool > checkFileExists( RecordID record_id, drogon::orm::DbClientPtr db );

enum class FileState
{
	//! File was not found at its respective hash
	FileNotFound,
	//! File's hash did not match it's record hash
	FileInvalidHash,
	//! File is valid
	FileValid
};

/**
 * @brief Checks that a record has a file and it's valid
 * @param record_id
 * @return
 */
ExpectedTask< FileState > validateFile( RecordID record_id );

/**
 * @brief Returns the mtime of the file im microseconds
 * @param path
 * @return
 */
[[nodiscard]] std::int64_t getLastWriteTime( const std::filesystem::path& path );

} // namespace idhan::filesystem