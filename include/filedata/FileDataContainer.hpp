//
// Created by kj16609 on 7/13/22.
//


#pragma once
#ifndef IDHAN_FILEDATACONTAINER_HPP
#define IDHAN_FILEDATACONTAINER_HPP


#include <memory> //shared_ptr
#include <filesystem> //filesystem

#include <QObject> //signals

#include "DatabaseModule/files/files.hpp" //Hash32


enum class IDHANBitFlags
{
	ARCHIVED = 0b0000000000000001, TRASHED = 0b0000000000000010, DELETED = 0b0000000000000100,
};

enum class IDHANRenderType
{
	UNKNOWN = 0, //Display error
	IMAGE, //Media player - Image
	VIDEO, //Media player - Video
	RENDERABLE, //Indicates processing needs to be done before it can be shown
};

struct FileDataContainer
{
	std::shared_ptr< std::mutex > modificationLock;

	uint64_t hash_id;

	Hash32 sha256;

	std::filesystem::path thumbnail_path {};
	bool thumbnail_valid { false };
	std::filesystem::path file_path {};

	//Flags of interest
	IDHANRenderType render_type { IDHANRenderType::UNKNOWN };
	IDHANBitFlags bitflags { 0 };

	FileDataContainer( const uint64_t, const std::shared_ptr< std::mutex > = std::make_shared< std::mutex >() );

	void signalModification();

signals:

	void updated( const uint64_t );
};


#endif //IDHAN_FILEDATACONTAINER_HPP
