//
// Created by kj16609 on 7/13/22.
//

#include "FileDataContainer.hpp"
#include "DatabaseModule/tags/mappings.hpp"


FileDataContainer::FileDataContainer( const uint64_t hash_id_, const std::shared_ptr< std::mutex > mtx_ptr )
	: modificationLock( mtx_ptr ),
	  hash_id( hash_id_ ),
	  sha256( files::async::getHash( hash_id_ ).result() ),
	  thumbnail_path( files::getThumbnailpath( hash_id_ ) ),
	  thumbnail_valid( std::filesystem::exists( thumbnail_path ) ),
	  file_path( files::getFilepath( hash_id_ ) ),
	  tags( mappings::async::getMappings( hash_id_ ).result() )
{
}


void FileDataContainer::signalModification()
{
	//emit updated();
}

