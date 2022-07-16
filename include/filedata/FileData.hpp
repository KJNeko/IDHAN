//
// Created by kj16609 on 7/9/22.
//


#pragma once
#ifndef IDHAN_FILEDATA_HPP
#define IDHAN_FILEDATA_HPP


#include <memory>

#include <QMetaType>

#include "database/tags/tags.hpp"
#include "database/files/files.hpp"

#include "FileDataContainer.hpp"
#include "FileDataPool.hpp"


class FileData : public std::shared_ptr< FileDataContainer >
{
	uint64_t hash_id_ { 0 };
public:

	FileData() = delete;


	FileData( uint64_t hash_id )
		: std::shared_ptr< FileDataContainer >( FileDataPool::request( hash_id ) ), hash_id_( hash_id ) {}


	FileData( const FileData& other );

	FileData( FileData&& other );

	~FileData();
};

Q_DECLARE_METATYPE( FileData )

#endif //IDHAN_FILEDATA_HPP