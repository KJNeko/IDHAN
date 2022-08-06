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


class UniqueConnection
{
	std::unique_ptr< pqxx::connection > connection;

public:

	pqxx::work transaction;


	UniqueConnection() : connection( ConnectionPool::acquire() ), transaction( *connection )
	{
	}


	~UniqueConnection()
	{

		ConnectionPool::release( connection.release() );
	}
};


class ConnectionManager
{
	inline static std::unordered_map< std::thread::id, std::shared_ptr< UniqueConnection > > activeConnections;

	inline static std::mutex connectionLock;

public:

	static std::shared_ptr< UniqueConnection > acquire()
	{

		const auto thread_id { std::this_thread::get_id() };

		const std::optional< std::shared_ptr< UniqueConnection>> conn_iter = [ & ]() -> std::optional< std::shared_ptr< UniqueConnection>>
		{
			std::lock_guard< std::mutex > lock( connectionLock );
			const auto it { activeConnections.find( thread_id ) };

			if ( it == activeConnections.end() )
			{
				return std::nullopt;
			}
			else
			{
				return it->second;
			}
		}();

		if ( conn_iter.has_value() )
		{
			return conn_iter.value();
		}
		else
		{
			std::shared_ptr< UniqueConnection > new_conn { std::make_shared< UniqueConnection >() };
			std::lock_guard< std::mutex > lock( connectionLock );

			activeConnections.emplace( thread_id, new_conn );

			return new_conn;
		}
	}


	static void release()
	{
		std::lock_guard< std::mutex > lock( connectionLock );

		const auto thread_id { std::this_thread::get_id() };

		const auto it { activeConnections.find( thread_id ) };

		if ( it->second.unique() || it->second.use_count() == 2 )
		{
			activeConnections.erase( thread_id );
		}
	}


};

class RecursiveConnection
{
public:

	std::shared_ptr< UniqueConnection > connection;


	[[nodiscard]] pqxx::work* getWork() const
	{
		return &( connection->transaction );
	}


	RecursiveConnection() : connection( ConnectionManager::acquire() )
	{

	}


	~RecursiveConnection()
	{
		ConnectionManager::release();
	}


};

typedef RecursiveConnection Connection;

namespace Database
{
	void initalizeConnection( const std::string& connectionArgs );

	void RESETDATABASE();
}


#endif // MAIN_DATABASE_HPP
