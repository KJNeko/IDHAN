//
// Created by kj16609 on 7/13/22.
//

#include "FileDataPool.hpp"

#include "FileDataContainer.hpp"

#include <mutex>
#include <memory>

#include "TracyBox.hpp"


std::shared_ptr< FileDataContainer > FileDataPool::request( const uint64_t hash_id )
{
	std::lock_guard< std::mutex > lock( filePoolLock );

	TracyCPlot( "FileDataPool Size", static_cast<double>(filePool.size()) );

	if ( filePool.contains( hash_id ) )
	{
		const auto itter { filePool.find( hash_id ) };
		return itter->second;
	}
	else
	{
		const auto shared_ptr { std::make_shared< FileDataContainer >( FileDataContainer( hash_id ) ) };

		if ( !filePool.insert( { hash_id, shared_ptr } ).second )
		{
			spdlog::error( "Unable to insert {} into filepool", hash_id );
		}

		return shared_ptr;
	}
}


void FileDataPool::invalidate( const uint64_t hash_id )
{
	std::lock_guard< std::mutex > lock( filePoolLock );
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

	const auto itter { filePool.find( hash_id ) };
	if ( itter != filePool.end() )
	{
		filePool.erase( itter );

		TracyCPlot( "FileDataPool Size", static_cast<double>(filePool.size()) );
	}
}
