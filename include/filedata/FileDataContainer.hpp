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


struct FileDataContainer
{
	std::shared_ptr< std::mutex > modificationLock;

	uint64_t hash_id;

	Hash32 sha256;

	std::filesystem::path thumbnail_path {};
	bool thumbnail_valid { false };
	std::filesystem::path file_path {};

	//Flags of interest
	bool is_video { false };
	bool is_inbox { false };

	FileDataContainer( const uint64_t, const std::shared_ptr< std::mutex > = std::make_shared< std::mutex >() );

	void signalModification();

signals:

	void updated( const uint64_t );
};


#endif //IDHAN_FILEDATACONTAINER_HPP
