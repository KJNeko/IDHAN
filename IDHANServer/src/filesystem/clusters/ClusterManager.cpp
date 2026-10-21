#include "ClusterManager.hpp"
#include "clusterIdentity.hpp"
#include "ClusterMarker.hpp"
#include <boost/uuid/string_generator.hpp>
#include <fcntl.h>

#include "fgl/defines.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/log.hpp"

namespace idhan::filesystem
{

//! Queried on each call rather than cached, so a cluster's free space does not go stale.
//! An unreadable path reports zero instead of throwing.
std::filesystem::space_info spaceInfo( const std::filesystem::path& path )
{
	std::error_code code {};
	std::filesystem::space_info info { std::filesystem::space( path, code ) };
	if ( code ) info = { 0, 0, 0 };
	return info;
}

std::size_t getFilesystemCapacity( const std::filesystem::path& path )
{
	return static_cast< std::size_t >( spaceInfo( path ).available );
}

std::size_t ClusterManager::ClusterInfo::capacity() const
{
	return static_cast< std::size_t >( spaceInfo( m_path ).capacity );
}

std::size_t ClusterManager::ClusterInfo::free() const
{
	return static_cast< std::size_t >( spaceInfo( m_path ).available );
}

ClusterManager::ClusterInfo::ClusterInfo( const std::filesystem::path& path, const ClusterID id ) :
  m_id( id ),
  m_path( path ),
  m_flags( ClusterFlags::STORES_DEFAULT ),
  m_max_capacity( std::numeric_limits< std::size_t >::max() )
{}

ClusterManager::ClusterManager()
{
	FGL_ASSERT( m_instance == nullptr, "Only one instance of cluster manager should exist" );
	m_instance = this;
}

runtime::Task< void > ClusterManager::reloadClusters( db::DBContext& db, const bool recreate_missing_markers )
{
	log::info( "Reloading clusters" );
	const auto clusters {
		co_await db.query( "SELECT cluster_id, folder_path, read_only, cluster_uuid::text FROM file_clusters" )
			.rows< ClusterID, std::string, bool, std::string >()
	};

	std::string failures;
	for ( const auto& [ cluster_id, folder_path, read_only, uuid_text ] : clusters )
	{
		if ( recreate_missing_markers )
		{
			std::error_code error;
			std::filesystem::create_directories( folder_path, error );
			if ( error )
			{
				failures += std::format(
					"\nCluster {}: cannot restore directory {}: {}", cluster_id, folder_path, error.message() );
				continue;
			}
			auto root { folly::File::makeFile( folder_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC ) };
			if ( root )
			{
				const auto marker {
					initializeClusterMarker( root->fd(), boost::uuids::string_generator {}( uuid_text ) )
				};
				if ( !marker ) failures += std::format( "\nCluster {}: {}", cluster_id, marker.error() );
			}
		}
		const auto access { co_await openCluster( db, cluster_id ) };
		if ( !access ) failures += std::format( "\nCluster {} at {}: {}", cluster_id, folder_path, access.error() );
	}
	if ( !failures.empty() )
		throw std::runtime_error(
			"Cluster verification failed. Restore the expected markers, or acknowledge missing-marker recovery in TOML with [cluster] recreate_missing_markers = true (self-resetting). Conflicting markers are never overwritten."
			+ failures );

	std::lock_guard lock { m_mutex };
	m_clusters.clear();

	for ( auto [ cluster_id, folder_path, read_only, uuid_text ] : clusters )
	{
		log::info( "Found cluster {}", folder_path );
		ClusterInfo info { folder_path, cluster_id };
		info.m_read_only = read_only;
		m_clusters.emplace( cluster_id, std::move( info ) );
	}
}

ClusterManager& ClusterManager::getInstance()
{
	return *m_instance;
}

} // namespace idhan::filesystem
