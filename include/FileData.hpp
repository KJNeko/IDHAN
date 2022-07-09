//
// Created by kj16609 on 7/9/22.
//


#pragma once
#ifndef IDHAN_FILEDATA_HPP
#define IDHAN_FILEDATA_HPP


#include <QCache>

#include <unordered_map>
#include <memory>
#include <filesystem>

#include "database/tags.hpp"
#include "database/files.hpp"


struct FileDataContainer
{
	std::shared_ptr< std::mutex > modificationLock;

	uint64_t hash_id;

	Hash32 sha256;

	std::filesystem::path thumbnail_path {};
	bool thumbnail_valid { false };
	std::filesystem::path file_path {};

	std::vector< Tag > tags {};

	//Flags of interest
	bool is_video { false };
	bool is_inbox { false };

	FileDataContainer( const uint64_t, const std::shared_ptr< std::mutex > = std::make_shared< std::mutex >() );
};

class FileDataPool
{
	inline static std::unordered_map< uint64_t, std::shared_ptr< FileDataContainer>> filePool;
	inline static std::mutex filePoolLock; //Prevents things like a request and clear operation at the same time
	//QCache is used to prevent rapidly accessed data from vanishing right away.
	//Grace of '500' items.
	inline static QCache< uint64_t, FileDataContainer > filePoolCache { 500 };


public:
	static std::shared_ptr< FileDataContainer > request( uint64_t );

	//Rebuilds the mapping in filePool if it exists
	static void invalidate( uint64_t );

	//Clears the mapping from filePool
	static void clear( uint64_t );
};

class FileData : public std::shared_ptr< FileDataContainer >
{

public:

	FileData( uint64_t hash_id ) : std::shared_ptr< FileDataContainer >( FileDataPool::request( hash_id ) ) {}


	~FileData();
};


#endif //IDHAN_FILEDATA_HPP
