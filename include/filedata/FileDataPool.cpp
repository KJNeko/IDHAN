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
	ZoneScoped;

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

			spdlog::debug( "filePool.size(): {}", filePool.size() );
			spdlog::debug( "filePool.bucket_count(): {}", filePool.bucket_count() );
			spdlog::debug( "filePool.load_factor(): {}", filePool.load_factor() );
			spdlog::debug( "filePool.max_load_factor(): {}", filePool.max_load_factor() );
		}

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
		//Check that the use count is 0
		if ( itter->second.use_count() == 0 )
		{
			filePool.erase( itter );
		}
		else
		{
			spdlog::warn( "Tried to erase {} but it was in use {} times", hash_id, itter->second.use_count() );
			throw std::runtime_error(
				"Tried to erase " +
					std::to_string( hash_id ) +
					" that was in use " +
					std::to_string( itter->second.use_count() ) +
					" times"
			);
		}
		TracyCPlot( "FileDataPool Size", static_cast<double>(filePool.size()) );
	}
}
