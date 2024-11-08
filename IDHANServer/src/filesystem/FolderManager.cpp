//
// Created by kj16609 on 8/10/24.
//

#include "FolderManager.hpp"

#include <QStorageInfo>

namespace idhan::filesystem
{

//! Returns the extension for a given file
QString getExtension( const FileRecord& record )
{}

std::size_t getFilesystemCapacity( QDir path )
{
	QStorageInfo info { path };
	return info.bytesAvailable();
}

std::size_t FolderManager::FolderInfo::capacity() const
{
	return m_info.bytesTotal();
}

std::size_t FolderManager::FolderInfo::free() const
{
	return m_info.bytesAvailable();
}

FolderManager::FolderInfo::FolderInfo( QDir path ) : m_info( path ), m_path( path ), m_max_capacity( 0 )
{}

void FolderManager::FolderInfo::storeFile( const SHA256& sha256, QIODevice* io, QString extension ) const
{
	// Append a `.` if there isn't one
	if ( !extension.startsWith( '.' ) ) extension.insert( 0, '.' );
	QFile file { m_path.filePath( createSubpath( sha256 ) + extension ) };

	file.write( io->readAll() );
}

FolderManager::FolderInfo& FolderManager::findBestFolder( const std::size_t file_size )
{
	//TODO: More advanced folder options
	return m_folders.at( 0 );
}

void FolderManager::storeFile( const FileRecord& record, QIODevice* data )
{
	const auto data_inital_cursor { data->pos() };
	data->seek( 0 );

	auto& folder { findBestFolder( data->size() ) };
	folder.storeFile( record.m_sha256, data, getExtension( record ) );

	data->seek( data_inital_cursor );
}

FolderManager& FolderManager::getInstance()
{
	static FolderManager manager;
	return manager;
}

} // namespace idhan::filesystem
