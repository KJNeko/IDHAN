//
// Created by kj16609 on 7/13/22.
//

#include "FileDataContainer.hpp"


FileDataContainer::FileDataContainer( const uint64_t hash_id_, const std::shared_ptr< std::mutex > mtx_ptr )
	: modificationLock( mtx_ptr ),
	  hash_id( hash_id_ ),
	  sha256( getHash( hash_id_ ) ),
	  thumbnail_path( getThumbnailpath( hash_id_ ) ),
	  thumbnail_valid( std::filesystem::exists( thumbnail_path ) ),
	  file_path( getFilepath( hash_id_ ) )
{
	ZoneScoped;
}


void FileDataContainer::signalModification()
{
	//emit updated();
}

