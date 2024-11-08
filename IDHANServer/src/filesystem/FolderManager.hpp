//
// Created by kj16609 on 8/10/24.
//

#pragma once

#include <QStorageInfo>

#include <cstdint>
#include <filesystem>
#include <future>
#include <queue>
#include <vector>

#include "import/import.hpp"

namespace idhan::filesystem
{

inline QString createSubpath( const SHA256& sha256 )
{
	const QString hex { sha256.hex() };

	// [0:1]/[2:3]/[0:128]
	QDir path {};
	path = path.filePath( hex.mid( 0, 2 ) );
	path = path.filePath( hex.mid( 2, 2 ) );
	return path.path();
}

class FolderManager
{
	struct FolderInfo
	{
		QStorageInfo m_info;
		QDir m_path;

		std::size_t capacity() const;

		std::size_t free() const;

		//! Max size this folder can contain. 0 == unlimited
		std::size_t m_max_capacity;

		FolderInfo( QDir path );

		void storeFile( const SHA256& sha256, QIODevice* io, QString extension ) const;

		bool hasFile( const SHA256& sha256 );
	};

	std::vector< FolderInfo > m_folders {};

	//! Finds the best folder to add the file too
	FolderInfo& findBestFolder( const std::size_t file_size );

  public:

	// Management
	void addFolder( const QDir path, const std::size_t max_size = 0 );
	void removeFolder( const QDir path );

	//File storage

	//! Stores the data located at `data`.
	/**
		 * @note Preserves the current index of `data`
		 */
	void storeFile( const FileRecord& record, QIODevice* data );

	std::ifstream openFile( const FileRecord& record );

	static FolderManager& getInstance();
};

} // namespace idhan::filesystem