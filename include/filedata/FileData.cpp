//
// Created by kj16609 on 7/9/22.
//

#include "FileData.hpp"

#include "FileDataPool.hpp"


FileData::~FileData()
{

	//Check if this is the last shared pointer to the original datapool
	if ( this->use_count() <= 2 )
	{
		FileDataPool::clear( hash_id_ );
	}

	//TracyFree( this );
}









