#pragma once

#include <drogon/HttpAppFramework.h>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/FunctionTraits.h>

#include <expected>
#include <filesystem>
#include <future>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan
{
class SHA256;
}

namespace idhan::filesystem
{
//! Manages the on-disk file "clusters" (storage directories). Maps each ClusterID to a directory,
//! picks the best writable cluster for a new file, and stores/retrieves files addressed by their
//! SHA-256. Process-wide singleton (see getInstance()).
class ClusterManager
{
	//! Capabilities a cluster directory may be flagged to store.
	enum ClusterFlags
	{
		STORES_THUMBNAILS = 1 << 0,
		STORES_ARCHIVES = 1 << 1,
		STORES_GENERATORS = 1 << 2,
		STORES_FILES = 1 << 3,

		STORES_ALL = STORES_THUMBNAILS | STORES_ARCHIVES | STORES_GENERATORS | STORES_FILES,
		STORES_DEFAULT = STORES_ALL
	};

	struct ClusterInfo
	{
		ClusterID m_id;
		std::filesystem::path m_path;

		ClusterFlags m_flags;

		//! Mirrors file_clusters.read_only. Files may only be read out of a read-only cluster, never
		//! written into it. Defaults to true so a cluster built without database state fails closed.
		bool m_read_only { true };

		[[nodiscard]] std::size_t capacity() const;

		[[nodiscard]] std::size_t free() const;

		ClusterInfo( const std::filesystem::path& path, ClusterID id );

		ClusterInfo( const drogon::orm::Row& row );

		//! Max size this folder can contain. 0 == unlimited
		std::size_t m_max_capacity;

		//! Writes \p data into this cluster, addressed by \p sha256.
		//! \return An error response if the cluster is read-only, or if the write itself fails.
		[[nodiscard]] std::expected< void, drogon::HttpResponsePtr > storeFile(
			const SHA256& sha256,
			const std::byte* data,
			std::size_t length,
			std::string_view extension ) const;

		[[nodiscard]] bool hasFile( const SHA256& sha256 ) const;
	};

	std::mutex m_mutex {};
	std::unordered_map< ClusterID, ClusterInfo > m_clusters {};

	inline static ClusterManager* m_instance;

  public:

	ClusterManager();

	//! Reloads the cluster info from the database
	drogon::Task< void > reloadClusters( DbClientPtr db );

	//! Finds the best folder to add the file too.
	[[nodiscard]] drogon::Task< std::expected< ClusterID, drogon::HttpResponsePtr > > findBestFolder(
		RecordID record_id,
		std::size_t file_size,
		DbClientPtr db );

	//! Stores the data located at `stream` for a given record id.
	//! \note A no-op reporting success if the record already sits in a read-only cluster and the file
	//! is present there: nothing may write to or delete from such a cluster, so re-storing would only
	//! add a second copy elsewhere and orphan the original.
	[[nodiscard]] drogon::Task< std::expected< void, drogon::HttpResponsePtr > > storeFile(
		RecordID record,
		const std::byte* data,
		std::size_t length,
		DbClientPtr db );

	//! \return The on-disk directory path for \p cluster_id, or an error response if it is unknown.
	[[nodiscard]] ExpectedTask< std::filesystem::path > getClusterPath( ClusterID cluster_id );

	//! \return The process-wide ClusterManager singleton.
	static ClusterManager& getInstance();
};
} // namespace idhan::filesystem
