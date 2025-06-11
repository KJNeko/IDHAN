//
// Created by kj16609 on 8/10/24.
//

#include "ClusterManager.hpp"

#include <QStorageInfo>

#include <fstream>

#include "../core/search/records.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "core/files/mime.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan::filesystem
{

std::size_t getFilesystemCapacity( QDir path )
{
	QStorageInfo info { path };
	return info.bytesAvailable();
}

std::size_t ClusterManager::ClusterInfo::capacity() const
{
	return m_info.bytesTotal();
}

std::size_t ClusterManager::ClusterInfo::free() const
{
	return m_info.bytesAvailable();
}

ClusterManager::ClusterInfo::ClusterInfo( std::filesystem::path path, const ClusterID id ) :
  m_id( id ),
  m_info( path ),
  m_path( path ),
  m_max_capacity( 0 )
{}

ClusterManager::ClusterInfo::ClusterInfo( const drogon::orm::Row& row ) :
  m_id( row[ "cluster_id" ].as< ClusterID >() ),
  m_info( QString::fromStdString( row[ "folder_path" ].as< std::string >() ) ),
  m_path( QString::fromStdString( row[ "folder_path" ].as< std::string >() ) )
{}

std::string metaTypePathID( const FileMetaType type )
{
	switch ( type )
	{
		case THUMBNAIL:
			return "t";
			break;
		default:
			[[fallthrough]];

			// case ARCHIVE:
			// [[fallthrough]];
			// case GENERATOR:
			// [[fallthrough]];
			// case GENERATED:
			// [[fallthrough]];
			// case VIRTUAL:
			// return "f";
		case NORMAL:
			return "f";
	}
}

std::filesystem::path createSubpath( const SHA256& sha256, const FileMetaType meta_type )
{
	const std::string hex { sha256.hex() };

	// (f/t)x{0,1}/{0-128}

	std::filesystem::path path { metaTypePathID( meta_type ) + hex.substr( 0, 2 ) };
	path /= hex;

	return path;
}

std::expected< void, drogon::HttpResponsePtr > ClusterManager::ClusterInfo::storeFile(
	const SHA256& sha256,
	const std::byte* data,
	const std::size_t length,
	std::string_view extension,
	const FileMetaType type ) const
{
	// Append a `.` if there isn't one

	// QFile file { m_path.filePath( createSubpath( sha256 ) + extension ) };
	auto path { m_path.filesystemAbsolutePath() / createSubpath( sha256, type ) };

	log::info( "Writing file to {}", path.string() );

	if ( extension.starts_with( '.' ) )
		path.replace_extension( extension );
	else
	{
		std::string ext { "." };
		ext += extension;
		path.replace_extension( ext );
	}

	if ( !std::filesystem::exists( path.parent_path() ) ) std::filesystem::create_directories( path.parent_path() );

	//TODO: mmap superiority

	if ( std::ofstream ofs( path, std::ios::binary ); ofs )
	{
		ofs.write( reinterpret_cast< const std::ostream::char_type* >( data ), length );

		ofs.flush();
		//TODO: Add flag for 'ensure write' to ensure we've always written the data fully to at least some media
	}
	else
		return std::unexpected(
			createInternalError( "Failed to open file {} for writing!", std::filesystem::absolute( path ).string() ) );

	return {};
}

drogon::Task< std::expected< ClusterID, drogon::HttpResponsePtr > > ClusterManager::
	findBestFolder( const RecordID record_id, const std::size_t file_size, drogon::orm::DbClientPtr db )
{
	const auto cluster_check { co_await db->execSqlCoro(
		"SELECT cluster_id, cluster_delete_time FROM file_info WHERE record_id = $1 LIMIT 1", record_id ) };

	const bool seen_before { cluster_check[ 0 ][ 0 ].isNull() };
	const bool file_deleted { cluster_check[ 0 ][ 1 ].isNull() };

	if ( seen_before && !file_deleted )
	{
		// We might still have the file, So we'll return the cluster it should be in.
		co_return cluster_check[ 0 ][ 0 ].as< ClusterID >();
	}

	const auto clusters { co_await db->execSqlCoro( "SELECT * FROM file_clusters" ) };

	if ( clusters.empty() )
		co_return std::
			unexpected( createBadRequest( "No clusters available, You must create one before importing files" ) );

	const auto rankCluster = []( const drogon::orm::Row& row ) -> std::size_t
	{
		const auto& size_used { row[ "size_used" ].as< std::size_t >() };
		const auto& size_total { row[ "size_limit" ].as< std::size_t >() };
		//TODO: Add free capacity to the ranking
		const double ratio_used = static_cast< double >( size_used ) / static_cast< double >( size_total );
		return static_cast< std::size_t >( ratio_used * 100 );
	};

	std::vector< std::pair< std::size_t, ClusterID > > cluster_scores {};

	for ( const auto& row : clusters )
	{
		cluster_scores.emplace_back( rankCluster( row ), row[ "cluster_id" ].as< ClusterID >() );
	}

	std::ranges::sort( cluster_scores, []( const auto& a, const auto& b ) { return a.first < b.first; } );

	co_return cluster_scores[ 0 ].second;
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > ClusterManager::storeFile(
	const RecordID record,
	const std::byte* data,
	const std::size_t length,
	drogon::orm::DbClientPtr db,
	const FileMetaType type )
{
	std::lock_guard lock { m_mutex };
	log::info( "Getting SHA256 for record {}", record );
	const auto sha256_e { co_await getRecordSHA256( record, db ) };
	if ( !sha256_e.has_value() ) co_return std::unexpected( sha256_e.error() );
	const auto& sha256 { sha256_e.value() };
	log::info( "Got SHA256 for record {}", record );

	const auto& target_cluster_r { co_await findBestFolder( record, length, db ) };

	if ( !target_cluster_r.has_value() ) co_return std::unexpected( target_cluster_r.error() );
	const auto& target_folder { m_folders.at( target_cluster_r.value() ) };

	log::info(
		"Found ideal cluster {} to write {} to", target_folder.m_path.absolutePath().toStdString(), sha256.hex() );

	const auto record_mime { co_await mime::getRecordMime( record, db ) };

	log::info( "Got record mime" );

	const auto result { target_folder.storeFile( sha256, data, length, record_mime.value().extension, type ) };

	if ( !result.has_value() ) co_return result;

	log::info( "File stored" );

	constexpr auto query { "UPDATE file_info SET cluster_store_time = now(), cluster_id = $2 WHERE record_id = $1" };

	log::info( "Updating file info for record {}", record );

	co_await db->execSqlCoro( query, record, target_folder.m_id );

	co_return {};
}

ClusterManager::ClusterManager()
{
	auto db { drogon::app().getDbClient() };

	drogon::sync_wait( reloadClusters( db ) );
	FGL_ASSERT( m_instance == nullptr, "Only one instance of cluster manager should exist" );
	m_instance = this;
}

drogon::Task< void > ClusterManager::reloadClusters( drogon::orm::DbClientPtr db )
{
	log::info( "Reloading clusters" );
	std::lock_guard lock { m_mutex };
	m_folders.clear();

	const auto clusters { co_await db->execSqlCoro( "SELECT * FROM file_clusters" ) };

	for ( const auto& cluster : clusters )
	{
		log::info( "Found cluster {}", cluster[ "folder_path" ].as< std::string >() );
		const auto cluster_id { cluster[ "cluster_id" ].as< ClusterID >() };
		m_folders.emplace( cluster_id, cluster );
	}
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > ClusterManager::
	storeFile( RecordID record, const std::byte* data, const std::size_t length, drogon::orm::DbClientPtr db )
{
	const auto result { co_await storeFile( record, data, length, db, FileMetaType::NORMAL ) };
	co_return result;
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > ClusterManager::
	storeThumbnail( RecordID record, const std::byte* data, const std::size_t length, drogon::orm::DbClientPtr db )
{
	co_return co_await storeFile( record, data, length, db, FileMetaType::THUMBNAIL );
}

ClusterManager& ClusterManager::getInstance()
{
	return *m_instance;
}

} // namespace idhan::filesystem
