#include "ClusterManager.hpp"

#include <boost/uuid/string_generator.hpp>

#include <fcntl.h>

#include "ClusterMarker.hpp"
#include "ClusterWrite.hpp"
#include "clusterIdentity.hpp"
#include "fgl/defines.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/log.hpp"

namespace idhan::filesystem
{

template < typename Database >
[[nodiscard]] runtime::Task< std::shared_ptr< const ClusterManager::Configuration > > readClusterConfiguration(
	Database& db )
{
	const auto rows {
		co_await db
			.query(
				"SELECT cluster_id, folder_path, cluster_uuid::text, read_only, cluster_name, ratio_number, size_limit FROM file_clusters" )
			.template rows<
				ClusterID,
				std::string,
				std::string,
				bool,
				std::optional< std::string >,
				std::int16_t,
				std::int64_t >()
	};
	auto configuration { std::make_shared< ClusterManager::Configuration >() };
	for ( const auto& [ id, path, uuid, readonly, name, ratio, limit ] : rows )
		configuration->emplace(
			id, ClusterInfo { id, path, boost::uuids::string_generator {}( uuid ), readonly, name, ratio, limit } );
	co_return configuration;
}

ClusterWrite::ClusterWrite(
	ClusterManager& manager,
	std::unique_lock< folly::coro::Mutex > lock,
	db::DBTransaction transaction ) :
  m_manager( manager ),
  m_lock( std::move( lock ) ),
  m_transaction( std::move( transaction ) )
{}

runtime::Task<> ClusterWrite::commit()
{
	// Allocate and decode before committing so failures leave both database and cache unchanged.
	auto configuration { co_await readClusterConfiguration( m_transaction ) };
	co_await m_transaction.commit();
	m_manager.m_clusters.store( std::move( configuration ) );
	m_lock.unlock();
}

runtime::Task<> ClusterWrite::rollback()
{
	co_await m_transaction.rollback();
	m_lock.unlock();
}

runtime::Task< ClusterWrite > ClusterManager::beginWrite( db::DBContext& db )
{
	auto lock { co_await m_write_mutex.co_scoped_lock() };
	auto transaction { co_await db.transaction() };
	co_return ClusterWrite { *this, std::move( lock ), std::move( transaction ) };
}

std::optional< ClusterInfo > ClusterManager::find( const ClusterID id ) const
{
	const auto configuration { m_clusters.load() };
	const auto found { configuration->find( id ) };
	if ( found == configuration->end() ) return std::nullopt;
	return found->second;
}

std::vector< ClusterInfo > ClusterManager::list() const
{
	const auto configuration { m_clusters.load() };
	std::vector< ClusterInfo > result;
	result.reserve( configuration->size() );
	for ( const auto& [ id, cluster ] : *configuration ) result.push_back( cluster );
	return result;
}

ClusterManager::ClusterManager()
{
	FGL_ASSERT( m_instance == nullptr, "Only one instance of cluster manager should exist" );
	m_instance = this;
}

runtime::Task< void > ClusterManager::reloadClusters( db::DBContext& db, const bool recreate_missing_markers )
{
	log::info( "Reloading clusters" );
	auto lock { co_await m_write_mutex.co_scoped_lock() };
	const auto clusters { co_await readClusterConfiguration( db ) };

	std::string failures;
	for ( const auto& [ cluster_id, info ] : *clusters )
	{
		const auto& folder_path { info.path };
		const auto read_only { info.read_only };
		const auto uuid { info.uuid };
		if ( recreate_missing_markers )
		{
			std::error_code error;
			std::filesystem::create_directories( folder_path, error );
			if ( error )
			{
				failures += std::format(
					"\nCluster {}: cannot restore directory {}: {}",
					cluster_id,
					folder_path.string(),
					error.message() );
				continue;
			}
			auto root { folly::File::makeFile( folder_path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC ) };
			if ( root )
			{
				const auto marker { initializeClusterMarker( root->fd(), uuid ) };
				if ( !marker ) failures += std::format( "\nCluster {}: {}", cluster_id, marker.error() );
			}
		}
		const auto access { ClusterAccess::open( folder_path, uuid, read_only ) };
		if ( !access )
			failures += std::format( "\nCluster {} at {}: {}", cluster_id, folder_path.string(), access.error() );
	}
	if ( !failures.empty() )
		throw std::runtime_error(
			"Cluster verification failed. Restore the expected markers, or acknowledge missing-marker recovery in TOML with [cluster] recreate_missing_markers = true (self-resetting). Conflicting markers are never overwritten."
			+ failures );

	m_clusters.store( clusters );
}

ClusterManager& ClusterManager::getInstance()
{
	return *m_instance;
}

} // namespace idhan::filesystem
