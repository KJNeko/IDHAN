//
// Created by kj16609 on 11/13/25.
//
#pragma once
#include <filesystem>

#include "io/IOUring.hpp"
#include "ipc/Blob.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan
{
class SHA256;
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

//! Maps a record's file into a blob that can be handed to a module worker.
/** Replaces the read-the-whole-file-into-a-vector pattern the module call sites used. The bytes are
 *  copied once, inside the kernel, into a sealed anonymous mapping; the server reads them through
 *  blob.view() for its own MIME scanning and passes the same object to the module, which receives
 *  only a pointer and a length.
 *
 *  The blob must outlive any module call made with it. That used to be a comment repeated at every
 *  call site; now it is just ordinary object lifetime. */
ExpectedTask< ipc::Blob > mapRecordBlob( RecordID record_id, DbClientPtr db );

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