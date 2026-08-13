#include "ClusterManager.hpp"

#include <fstream>
#include <optional>

#include "api/helpers/createBadRequest.hpp"
#include "core/files/mime.hpp"
#include "core/record/getRecordSHA256.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/defines.hpp"
#include "filesystem/filesystem.hpp"
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

ClusterManager::ClusterInfo::ClusterInfo( const drogon::orm::Row& row ) :
  m_id( row[ "cluster_id" ].as< ClusterID >() ),
  m_path( row[ "folder_path" ].as< std::string >() ),
  m_flags( ClusterFlags::STORES_DEFAULT ),
  m_read_only( row[ "read_only" ].as< bool >() ),
  m_max_capacity( std::numeric_limits< std::size_t >::max() )
{}

//! Must stay in step with getFileFolder(), which resolves the same layout when reading a file back.
std::filesystem::path createSubpath( const SHA256& sha256 )
{
	const std::string hex { sha256.hex() };

	// fx{0,1}/{0-128}

	std::filesystem::path path { "f" + hex.substr( 0, 2 ) };
	path /= hex;

	return path;
}

std::expected< void, drogon::HttpResponsePtr > ClusterManager::ClusterInfo::storeFile(
	const SHA256& sha256,
	const std::byte* data,
	const std::size_t length,
	std::string_view extension ) const
{
	// Last line of defence: findBestFolder already refuses to hand back a read-only cluster, but it
	// is not the only way to reach a ClusterInfo, and the cluster can be flipped read-only between
	// selection and here. Guarding the write itself means no caller can bypass the rule.
	if ( m_read_only )
		return std::unexpected( createInternalError( "Refusing to write into read-only cluster {}", m_id ) );

	// Append a `.` if there isn't one

	// QFile file { m_path.filePath( createSubpath( sha256 ) + extension ) };
	auto path { std::filesystem::absolute( m_path ) / createSubpath( sha256 ) };

	if ( extension.starts_with( '.' ) )
		path.replace_extension( extension );
	else
	{
		std::string ext { "." };
		ext += extension;
		path.replace_extension( ext );
	}

	if ( !std::filesystem::exists( path.parent_path() ) ) std::filesystem::create_directories( path.parent_path() );

	// TODO: IOuring write wrapper

	if ( std::ofstream ofs( path, std::ios::binary ); ofs )
	{
		ofs.write( reinterpret_cast< const std::ostream::char_type* >( data ), length );

		ofs.flush();
		// TODO: Add flag for 'ensure write' to ensure we've always written the data fully to at least some media
	}
	else
		return std::unexpected(
			createInternalError( "Failed to open file {} for writing!", std::filesystem::absolute( path ).string() ) );

	return {};
}

drogon::Task< std::expected< ClusterID, drogon::HttpResponsePtr > > ClusterManager::findBestFolder(
	const RecordID record_id,
	[[maybe_unused]] const std::size_t file_size,
	DbClientPtr db )
{
	const auto cluster_check { co_await db->execSqlCoro(
		"SELECT file_info.cluster_id, file_info.cluster_delete_time, file_clusters.read_only "
		"FROM file_info LEFT JOIN file_clusters ON file_clusters.cluster_id = file_info.cluster_id "
		"WHERE file_info.record_id = $1 LIMIT 1",
		record_id ) };

	if ( !cluster_check.empty() )
	{
		const auto& row { cluster_check[ 0 ] };

		const bool has_cluster { !row[ "cluster_id" ].isNull() };
		const bool file_deleted { !row[ "cluster_delete_time" ].isNull() };
		// null when the record has no cluster at all, since the join then matches nothing
		const bool writable { !row[ "read_only" ].isNull() && !row[ "read_only" ].as< bool >() };

		if ( has_cluster && !file_deleted && writable )
		{
			// We might still have the file, So we'll return the cluster it should be in.
			co_return row[ "cluster_id" ].as< ClusterID >();
		}
	}

	const auto clusters { co_await db->execSqlCoro( "SELECT * FROM file_clusters WHERE read_only = FALSE" ) };

	if ( clusters.empty() )
	{
		const auto total { co_await db->execSqlCoro( "SELECT count(*) FROM file_clusters" ) };

		if ( total[ 0 ][ 0 ].as< std::int64_t >() == 0 )
			co_return std::unexpected(
				createBadRequest( "No clusters available, You must create one before importing files" ) );

		co_return std::unexpected(
			createBadRequest( "All clusters are read-only, You must mark one as writable before importing files" ) );
	}

	const auto rankCluster = []( const drogon::orm::Row& row ) -> double
	{
		const auto size_used { row[ "size_used" ].as< std::int64_t >() };
		const auto size_limit { row[ "size_limit" ].as< std::int64_t >() };
		// TODO: Add free capacity to the ranking
		if ( size_limit <= 0 ) return 0.0;
		return static_cast< double >( size_used ) / static_cast< double >( size_limit );
	};

	std::vector< std::pair< double, ClusterID > > cluster_scores {};

	for ( const auto& row : clusters )
	{
		cluster_scores.emplace_back( rankCluster( row ), row[ "cluster_id" ].as< ClusterID >() );
	}

	// Sort by lowest score = better
	std::ranges::sort(
		cluster_scores, []( const auto& a, const auto& b ) noexcept -> bool { return a.first < b.first; } );

	co_return cluster_scores[ 0 ].second;
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > ClusterManager::storeFile(
	const RecordID record,
	const std::byte* data,
	const std::size_t length,
	const DbClientPtr db )
{
	const auto sha256_e { co_await getRecordSHA256( record, db ) };
	if ( !sha256_e ) co_return std::unexpected( sha256_e.error() );
	const auto& sha256 { sha256_e.value() };

	// A record already held in a read-only cluster stays there. Storing it again cannot replace the
	// original -- nothing may write to or delete from a read-only cluster -- so it would only add a
	// second copy in a writable cluster and repoint file_info at it, orphaning the first. Report
	// success without writing instead.
	const auto current_cluster { co_await db->execSqlCoro(
		"SELECT file_clusters.read_only FROM file_info "
		"JOIN file_clusters ON file_clusters.cluster_id = file_info.cluster_id "
		"WHERE file_info.record_id = $1",
		record ) };

	if ( !current_cluster.empty() && current_cluster[ 0 ][ "read_only" ].as< bool >() )
	{
		const auto exists { co_await checkFileExists( record, db ) };
		return_unexpected_error( exists );

		if ( exists.value() )
		{
			log::debug( "Record {} is already stored in a read-only cluster, not storing it again", record );
			co_return {};
		}
	}

	const auto& target_id { co_await findBestFolder( record, length, db ) };
	return_unexpected_error( target_id );

	// copied out under lock rather than held by reference: a concurrent reloadClusters()
	// could otherwise invalidate the reference while we're still using it below
	std::optional< ClusterInfo > target_cluster_copy {};
	{
		std::lock_guard lock { m_mutex };
		const auto itter { m_clusters.find( *target_id ) };
		if ( itter == m_clusters.end() )
			co_return std::unexpected( createInternalError( "Failed to find cluster with id {}", *target_id ) );
		target_cluster_copy = itter->second;
	}
	const auto& target_cluster { *target_cluster_copy };

	log::debug( "Storing file for record {} in cluster {}", record, *target_id );
	const auto record_mime { co_await mime::getRecordMime( record, db ) };
	return_unexpected_error( record_mime );

	const auto result { target_cluster.storeFile( sha256, data, length, record_mime.value().extension ) };

	if ( !result ) co_return result;

	// clear cluster_delete_time: the record is stored again, and cluster_id_xor_delete_time
	// forbids a row having both a cluster and a delete time
	constexpr auto query {
		"UPDATE file_info SET cluster_store_time = now(), cluster_id = $2, cluster_delete_time = NULL WHERE record_id = $1"
	};

	co_await db->execSqlCoro( query, record, target_cluster.m_id );

	co_return {};
}

ClusterManager::ClusterManager()
{
	FGL_ASSERT( m_instance == nullptr, "Only one instance of cluster manager should exist" );
	m_instance = this;
}

drogon::Task< void > ClusterManager::reloadClusters( DbClientPtr db )
{
	log::info( "Reloading clusters" );
	const auto clusters { co_await db->execSqlCoro( "SELECT * FROM file_clusters" ) };

	std::lock_guard lock { m_mutex };
	m_clusters.clear();

	for ( const auto& cluster : clusters )
	{
		log::info( "Found cluster {}", cluster[ "folder_path" ].as< std::string >() );
		const auto cluster_id { cluster[ "cluster_id" ].as< ClusterID >() };
		m_clusters.emplace( cluster_id, cluster );
	}
}

ExpectedTask< std::filesystem::path > ClusterManager::getClusterPath( const ClusterID cluster_id )
{
	std::lock_guard lock { m_mutex };
	const auto itter { m_clusters.find( cluster_id ) };
	if ( itter == m_clusters.end() )
		co_return std::unexpected( createBadRequest( "Invalid cluster id {}", cluster_id ) );

	co_return std::filesystem::absolute( itter->second.m_path );
}

ClusterManager& ClusterManager::getInstance()
{
	return *m_instance;
}

} // namespace idhan::filesystem
