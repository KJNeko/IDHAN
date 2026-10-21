#pragma once

#include <filesystem>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan
{
class SHA256;
}

namespace idhan::db
{
class DBContext;
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

		//! Max size this folder can contain. 0 == unlimited
		std::size_t m_max_capacity;
	};

	std::mutex m_mutex {};
	std::unordered_map< ClusterID, ClusterInfo > m_clusters {};

	inline static ClusterManager* m_instance;

  public:

	ClusterManager();

	runtime::Task< void > reloadClusters( db::DBContext& db, bool recreate_missing_markers = false );

	static ClusterManager& getInstance();
};
} // namespace idhan::filesystem
