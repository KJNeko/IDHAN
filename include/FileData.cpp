//
// Created by kj16609 on 7/9/22.
//

#include "FileData.hpp"


FileDataContainer::FileDataContainer( const uint64_t hash_id_, const std::shared_ptr< std::mutex > mtx_ptr )
	: modificationLock( mtx_ptr ),
	  hash_id( hash_id_ ),
	  sha256( getHash( hash_id_ ) ),
	  thumbnail_path( getThumbnailpath( hash_id_ ) ),
	  thumbnail_valid( std::filesystem::exists( thumbnail_path ) ),
	  file_path( getFilepath( hash_id_ ) ),
	  tags( getTags( hash_id_ ) )
{
	ZoneScoped;

	//Calculate size of all the internal objects
	size_t size { 0 };

	size += sizeof( modificationLock ) + sizeof( std::mutex );
	size += sizeof( hash_id );
	size += sizeof( sha256 );
	size += sizeof( thumbnail_path ) + thumbnail_path.string().size();
	size += sizeof( file_path ) + file_path.string().size();
	size += sizeof( tags ) * tags.size();
	size += sizeof( thumbnail_valid );

	//TracyAlloc( this, size );
}


std::shared_ptr< FileDataContainer > FileDataPool::request( const uint64_t hash_id )
{
	std::lock_guard< std::mutex > lock( filePoolLock );
	ZoneScoped;

	const auto itter { filePool.find( hash_id ) };
	if ( itter != filePool.end() )
	{
		return itter->second;
	}
	else
	{
		//Create a new object and add it to the QCache

		const auto newObj { FileDataContainer( hash_id ) };
		const auto shared_ptr { std::make_shared< FileDataContainer >( newObj ) };

		filePool.emplace( std::make_pair( hash_id, shared_ptr ) );

		return shared_ptr;

	}
}


void FileDataPool::invalidate( const uint64_t hash_id )
{
	std::lock_guard< std::mutex > lock( filePoolLock );
	ZoneScoped;
	const auto itter { filePool.find( hash_id ) };
	if ( itter != filePool.end() )
	{
		auto old_ptr = itter->second;
		std::lock_guard< std::mutex > lock_obj( *old_ptr->modificationLock );

		//Create a new object
		FileDataContainer new_obj( hash_id, old_ptr->modificationLock );

		//Swap the new object with the old one
		std::swap( new_obj, *old_ptr );
	}
}


void FileDataPool::clear( const uint64_t hash_id )
{
	std::lock_guard< std::mutex > lock( filePoolLock );
	ZoneScoped;

	const auto itter { filePool.find( hash_id ) };
	if ( itter != filePool.end() )
	{
		filePool.erase( itter );
	}
}


FileData::~FileData()
{
	//Check if this is the last shared pointer to the original datapool
	if ( this->use_count() <= 2 )
	{
		FileDataPool::clear( hash_id_ );
	}

	//TracyFree( this );
}









