//
// Created by kj16609 on 8/10/24.
//

#pragma once

#include <QStorageInfo>

#include <filesystem>
#include <future>
#include <queue>
#include <vector>

#include "IDHANTypes.hpp"
#include "drogon/orm/DbClient.h"

namespace idhan
{
class SHA256;
}

namespace idhan::filesystem
{

/**
 * @brief The meta type of file. This is used internally for knowing how to handle certain files.
 *
 * @mainpage "IDHAN File MetaTypes"
 * A meta type is what IDHAN uses to refer to special files that have a specific purpose or that can create other files.
 * # Thumbnail
 * What you would expect...
 * # Normal
 * A normal file
 * # Archive
 * A file that contains other files, (zip, rar, ect)
 * # Generator
 * A file that has the ability to generate files. This is different from an archive as it means the file needs additional processing or flags in order to produce a number of results. The most common example of this is a psd file.
 * # Virtual
 * This is a file that is not stored in any cluster but is instead generated by a generator.
 *
 */
enum FileMetaType
{
	//! 0: Thumbnail of a stored file
	THUMBNAIL = 0,
	//! 1: A normal file.
	NORMAL = 1,
	//! 2: A file that contains a set of other files using compression or bundling (Example: zip)
	// ARCHIVE = 2,
	//! 3: A file that is capable of generating other files (Example: psd)
	//! @note This is specifically for files that are capable of producing variants using configuration options while producing output using another tool.
	// GENERATOR = 3,
	//! 4: A file that has been generated by a GENERATOR (Example: png from a psd)
	//! @note This is identical to a file. The only difference being that it has been generated by IDHAN at some point. A record is capable of going from FILE to GENERATED if the file hash is detected to be obtainable from a GENERATOR
	// GENERATED = 4,
	//! 5: This file type is identical to GENERATED except that it is not stored on the disk. Every time the file is requested it is regenerated.
	// VIRTUAL = 5,
};

class ClusterManager
{
	enum ClusterFlags
	{
		STORES_THUMBNAILS = 1 << 0,
		STORES_ARCHIVES = 1 << 1,
		STORES_GENERATORS = 1 << 2,
		STORES_FILES = 1 << 3,

		STORES_ALL = STORES_THUMBNAILS | STORES_ARCHIVES | STORES_GENERATORS | STORES_FILES
	};

	struct ClusterInfo
	{
		QStorageInfo m_info;
		QDir m_path;

		ClusterFlags m_flags;

		std::size_t capacity() const;

		std::size_t free() const;

		//! Max size this folder can contain. 0 == unlimited
		std::size_t m_max_capacity;

		ClusterInfo( std::filesystem::path path );

		void storeFile( const SHA256& sha256, std::istream& io, std::string_view extension, FileMetaType type ) const;

		bool hasFile( const SHA256& sha256 ) const;
	};

	std::vector< ClusterInfo > m_folders {};

	enum ClusterTargetType
	{
		THUMBNAIL,
		ARCHIVES,
		GENERATORS,
		FILES
	};

	//! Finds the best folder to add the file too.
	ClusterInfo& findBestFolder( std::size_t file_size );

	void storeFile( RecordID record, std::istream& stream, drogon::orm::DbClientPtr db, FileMetaType type );

  public:

	//File storage

	//! Stores the data located at `stream` for a given record id.
	void storeFile( RecordID record, std::istream& stream, drogon::orm::DbClientPtr db );
	void storeThumbnail( RecordID record, std::istream& stream, drogon::orm::DbClientPtr db );

	std::ifstream openFile( const RecordID record );

	static ClusterManager& getInstance();
};

} // namespace idhan::filesystem