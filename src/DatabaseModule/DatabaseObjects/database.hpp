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

#include <iostream>


#define CONNECTION_MAX_COUNT 32

class ConnectionPool
{

	inline static std::queue< std::unique_ptr< pqxx::connection > > connections {};

	inline static std::counting_semaphore< CONNECTION_MAX_COUNT > readyConnections { 0 };

	inline static std::mutex poolLock;

	inline static std::atomic< bool > initalized { false };

public:
	static pqxx::connection* acquire()
	{
		readyConnections.acquire();
		std::lock_guard< std::mutex > guard( poolLock );

		auto conn { connections.front().release() };
		connections.pop();
		return conn;
	}


	static void release( pqxx::connection* conn )
	{
		std::lock_guard< std::mutex > guard( poolLock );

		connections.emplace( conn );
		readyConnections.release();
	}


	static void init( const std::string& connString )
	{
		if ( initalized )
		{
			return;
		}

		std::lock_guard< std::mutex > lock( poolLock );
		const uint64_t connectionCount {
			std::min< unsigned int >( std::thread::hardware_concurrency(), CONNECTION_MAX_COUNT ) };
		for ( size_t i = 0; i < connectionCount; ++i )
		{
			connections.push( std::make_unique< pqxx::connection >( connString ) );
			readyConnections.release();
		}
		initalized = true;
	}


	static void deinit()
	{
		if ( !initalized )
		{
			return;
		}
		std::lock_guard< std::mutex > lock( poolLock );
		for ( size_t i = 0; i < connections.size(); ++i )
		{
			delete acquire();
		}
		initalized = false;
	}

};


struct UniqueConnection
{
	std::unique_ptr< pqxx::connection > connection;


	UniqueConnection() : connection( ConnectionPool::acquire() )
	{
	}


	~UniqueConnection()
	{
		ConnectionPool::release( connection.release() );
	}
};


namespace Database
{
	void initalizeConnection( const std::string& connectionArgs );

	void RESETDATABASE();
}


#endif // MAIN_DATABASE_HPP
