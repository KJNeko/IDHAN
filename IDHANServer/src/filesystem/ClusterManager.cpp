//
// Created by kj16609 on 8/10/24.
//

#include "ClusterManager.hpp"

#include <QStorageInfo>

#include <fstream>

#include "core/files/mime.hpp"
#include "core/records.hpp"
#include "crypto/SHA256.hpp"

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

ClusterManager::ClusterInfo::ClusterInfo( std::filesystem::path path ) :
  m_info( path ),
  m_path( path ),
  m_max_capacity( 0 )
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

void ClusterManager::ClusterInfo::
	storeFile( const SHA256& sha256, std::istream& io, std::string_view extension, const FileMetaType type ) const
{
	// Append a `.` if there isn't one

	// QFile file { m_path.filePath( createSubpath( sha256 ) + extension ) };
	auto path { createSubpath( sha256, type ) };

	if ( extension.starts_with( '.' ) )
		path.replace_extension( extension );
	else
	{
		std::string ext { "." };
		ext += extension;
		path.replace_extension( ext );
	}

	//check that io stream is in binary mode
	//TODO: Fix deprecated enum-enum-conversion (std::ios_base::fmtflags & std::ios_base::openmode)
	if ( !( io.flags() & std::ios::binary ) )
	{
		throw std::runtime_error( "IO was not in binary mode!" );
	}

	if ( std::ofstream ofs( path, std::ios::binary ); ofs )
	{
		const auto cursor { io.tellg() };
		io.seekg( 0, std::ios::beg );

		std::array< char, 1024 > bytes {};

		while ( io.good() )
		{
			io.readsome( bytes.data(), bytes.size() );
			ofs.write( bytes.data(), io.gcount() );
		}

		//restore cursor
		io.seekg( cursor, std::ios::beg );

		//TODO: Ensure that we have written the file before we return. (sync on linux, and whatever the fuck on windows)
	}
	else
		throw std::runtime_error( "Failed to open file!" );
}

ClusterManager::ClusterInfo& ClusterManager::findBestFolder( const std::size_t file_size )
{
	assert( !m_folders.empty() );
	return m_folders.at( 0 );
}

void ClusterManager::
	storeFile( const RecordID record, std::istream& stream, drogon::orm::DbClientPtr db, const FileMetaType type )
{
	const auto sha256 { getRecordSHA256( record, db ) };

	const auto cursor { stream.tellg() };
	stream.seekg( 0, std::ios::end );

	const auto size { stream.tellg() };
	stream.seekg( cursor, std::ios::beg );

	const auto& target_folder { findBestFolder( size ) };

	target_folder.storeFile( sha256, stream, mime::getRecordMime( record, db ).extension, type );
}

void ClusterManager::storeFile( RecordID record, std::istream& stream, drogon::orm::DbClientPtr db )
{
	return storeFile( record, stream, db, FileMetaType::NORMAL );
}

void ClusterManager::storeThumbnail( RecordID record, std::istream& stream, drogon::orm::DbClientPtr db )
{
	return storeFile( record, stream, db, FileMetaType::THUMBNAIL );
}

ClusterManager& ClusterManager::getInstance()
{
	static ClusterManager manager;
	return manager;
}

} // namespace idhan::filesystem
