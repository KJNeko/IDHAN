//
// Created by kj16609 on 7/9/22.
//

#include "FileData.hpp"


FileDataContainer::FileDataContainer( const uint64_t hash_id_, const std::shared_ptr< std::mutex > mtx_ptr )
	: modificationLock( mtx_ptr ), hash_id( hash_id_ )
{

}


std::shared_ptr< FileDataContainer > FileDataPool::request( const uint64_t hash_id )
{
	std::lock_guard< std::mutex > lock( filePoolLock );

	const auto itter { filePool.find( hash_id ) };
	if ( itter != filePool.end() )
	{
		return itter->second;
	}
	else
	{
		//Check if it's in the QCache first
		const auto obj { filePoolCache.object( hash_id ) };
		if ( obj )
		{
			auto shared_ptr { std::make_shared< FileDataContainer >( *obj ) };

			filePool.emplace( std::make_pair( hash_id, shared_ptr ) );
			return shared_ptr;
		}
		else
		{
			//Create a new object and add it to the QCache

			const auto newObj { FileDataContainer( hash_id ) };
			const auto shared_ptr { std::make_shared< FileDataContainer >( newObj ) };

			filePool.emplace( std::make_pair( hash_id, shared_ptr ) );
			filePoolCache.insert( hash_id, new FileDataContainer( newObj ) );

			return shared_ptr;
		}
	}
}


void FileDataPool::invalidate( const uint64_t hash_id )
{
	std::lock_guard< std::mutex > lock( filePoolLock );

	const auto itter { filePool.find( hash_id ) };
	if ( itter != filePool.end() )
	{
		auto old_ptr = itter->second;
		std::lock_guard< std::mutex > lock( *old_ptr->modificationLock );

		//Create a new object
		FileDataContainer new_obj( hash_id, old_ptr->modificationLock );

		//Swap the new object with the old one
		std::swap( new_obj, *old_ptr );
	}
}


void FileDataPool::clear( const uint64_t hash_id )
{
	std::lock_guard< std::mutex > lock( filePoolLock );

	const auto itter { filePool.find( hash_id ) };
	if ( itter != filePool.end() )
	{
		filePool.erase( itter );
	}
}


FileData::FileData( const uint64_t hash_id ) : data( FileDataPool::request( hash_id ) )
{

}


FileData::~FileData()
{
	//Check if this is the last shared pointer to the original datapool
	if ( data.use_count() <= 2 )
	{
		FileDataPool::clear( data->hash_id );
	}
}









