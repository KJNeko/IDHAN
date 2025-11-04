//
// Created by kj16609 on 10/30/25.
//
#pragma once
#include "IDHANTypes.hpp"
#include "api/helpers/ExpectedTask.hpp"
#include "crypto/SHA256.hpp"

namespace idhan::filesystem
{
/**
 *
 * @param record_id Record of which to get a filepath for
 * @param db
 * @return
 */
ExpectedTask< std::filesystem::path > getFilepath( RecordID record_id, DbClientPtr db );

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
std::int64_t getLastWriteTime( const std::filesystem::path& path );

} // namespace idhan::filesystem
