//
// Created by kj16609 on 7/9/22.
//

#include "FileData.hpp"

#include "FileDataPool.hpp"


//copy
FileData::FileData( const FileData& other )
	: std::shared_ptr< FileDataContainer >( other ), hash_id_( other.hash_id_ ) {}


FileData::FileData( FileData&& other )
	: std::shared_ptr< FileDataContainer >( std::move( other ) ), hash_id_( std::move( other.hash_id_ ) ) {}


FileData::~FileData()
{

	//Check if this is the last shared pointer to the original datapool
	if ( this->use_count() <= 2 )
	{
		FileDataPool::clear( hash_id_ );
	}

	//TracyFree( this );
}









