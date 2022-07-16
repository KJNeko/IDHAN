//
// Created by kj16609 on 6/1/22.
//

#pragma once
#ifndef MAIN_DATABASE_HPP
#define MAIN_DATABASE_HPP

// Don't make me box you
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wrestrict"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wmultiple-inheritance"
#pragma GCC diagnostic ignored "-Wswitch-default"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wpadded"
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"


#include <pqxx/pqxx>


#pragma GCC diagnostic pop


#include <mutex>
#include <semaphore>
#include <queue>
#include <thread>

#include "TracyBox.hpp"


class ConnectionManager
{
	inline static std::unique_ptr< pqxx::connection > connection;

	inline static std::shared_ptr< pqxx::work > work_global;

public:
	inline static std::recursive_mutex connectionLock;


	inline static std::shared_ptr< pqxx::work > acquireWork()
	{
		if ( work_global == nullptr )
		{
			spdlog::debug( "Beginning new work transaction" );
			work_global = std::make_shared< pqxx::work >( *connection );
		}
		return work_global;
	}


	inline static void markFinished()
	{
		if ( work_global.unique() )
		{
			auto temp = std::move( work_global );
			work_global = nullptr;
		}
	}


	inline static void init( const std::string& connectionString )
	{
		connection = std::make_unique< pqxx::connection >( connectionString );
	}
};

class Connection
{
	std::lock_guard< std::recursive_mutex > lock;

	std::shared_ptr< pqxx::work > work;

public:

	Connection() : lock( ConnectionManager::connectionLock ), work( ConnectionManager::acquireWork() ) {}


	std::shared_ptr< pqxx::work > getWork()
	{
		return work;
	}


	~Connection()
	{
		ConnectionManager::markFinished();
	}
};

namespace Database
{
	void initalizeConnection( const std::string& connectionArgs );
}

#endif // MAIN_DATABASE_HPP
