//
// Created by kj16609 on 7/13/22.
//


#pragma once
#ifndef IDHAN_FILEDATAPOOL_HPP
#define IDHAN_FILEDATAPOOL_HPP


#include <memory>
#include <mutex>
#include <unordered_map>

#include "FileDataContainer.hpp"


class FileDataPool
{
	inline static std::unordered_map< uint64_t, std::shared_ptr< FileDataContainer>> filePool;
	inline static std::mutex filePoolLock; //Prevents things like a request and clear operation at the same time

public:
	static std::shared_ptr< FileDataContainer > request( uint64_t );

	//Rebuilds the mapping in filePool if it exists
	static void invalidate( uint64_t );

	//Clears the mapping from filePool
	static void clear( uint64_t );
};


#endif //IDHAN_FILEDATAPOOL_HPP
