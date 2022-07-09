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


#define POOL_COUNT static_cast<uint64_t>(32)

class ConnectionPool
{

	inline static std::queue< pqxx::connection > connections;
	inline static std::mutex connections_mutex;
	inline static std::counting_semaphore< POOL_COUNT > active = std::counting_semaphore< POOL_COUNT >( POOL_COUNT );

public:
	static void init( const std::string& connString )
	{
		std::lock_guard< std::mutex > lock( connections_mutex );
		for ( uint64_t i = 0; i < POOL_COUNT; i++ )
		{
			connections.push( pqxx::connection( connString ) );
		}
	}


	static pqxx::connection acquire()
	{
		active.acquire();
		std::lock_guard< std::mutex > lock( connections_mutex );
		auto connection = std::move( connections.front() );
		connections.pop();

		return std::move( std::move( connection ) );
	}


	static void release( pqxx::connection& conn )
	{
		std::lock_guard< std::mutex > lock( connections_mutex );
		connections.push( std::move( conn ) );
		active.release();
	}
};

class Connection
{
	std::unique_ptr< pqxx::connection > conn { nullptr };

public:
	Connection()
	{
		conn = std::make_unique< pqxx::connection >( ConnectionPool::acquire() );
	}


	~Connection()
	{
		ConnectionPool::release( *conn.get() );
	}


	pqxx::connection& operator()()
	{
		return *conn;
	}
};

namespace Database
{
	void initalizeConnection( const std::string& connectionArgs );
}

#endif // MAIN_DATABASE_HPP
