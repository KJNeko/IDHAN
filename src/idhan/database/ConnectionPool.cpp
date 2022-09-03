//
// Created by kj16609 on 9/3/22.
//

#include "idhan/database/ConnectionPool.hpp"
#include <spdlog/spdlog.h>

namespace idhan::database
{

ConnectionPool::ConnectionPool( const std::string& conn_args )
{
	init( conn_args );
}

//! Acquires a ready connection from the pool
pqxx::connection* ConnectionPool::acquire()
{
	ready_connections.acquire();

	std::lock_guard< std::mutex > guard( pool_lock );

	auto conn{ std::move( connections.front() ) };

	connections.pop();

	return conn.release();
}

//! Releases a connection back into the pool
void ConnectionPool::release( pqxx::connection* conn )
{
	std::lock_guard< std::mutex > guard( pool_lock );

	connections.push( std::unique_ptr< pqxx::connection >( conn ) );

	ready_connections.release();
}

//! Initalizes the ConnectionPool with specific settings
void ConnectionPool::init( const std::string& conn_string )
{
	spdlog::info( "Creating {} connections with params '{}'", CONNECTION_MAX_COUNT, conn_string );
	if ( initalized ) { return; }

	for ( size_t i = 0; i < CONNECTION_MAX_COUNT; ++i )
	{
		connections.push( std::make_unique< pqxx::connection >( conn_string ) );
		ready_connections.release();
	}
}

//! Deinitalizes the ConnectionPool
void ConnectionPool::deinit()
{
	if ( !initalized ) { return; }

	for ( size_t i = 0; i < CONNECTION_MAX_COUNT; ++i )
	{
		ready_connections.acquire();
		std::lock_guard< std::mutex > guard( pool_lock );
		connections.pop();
	}
}

ConnectionPool::~ConnectionPool()
{
	deinit();
}

}