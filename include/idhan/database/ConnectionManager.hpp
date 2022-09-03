//
// Created by kj16609 on 9/3/22.
//


#pragma once
#ifndef IDHAN_CONNECTIONMANAGER_HPP
#define IDHAN_CONNECTIONMANAGER_HPP

#include <unordered_map>
#include <mutex>
#include <thread>

#include <idhan/database/UniqueConnection.hpp>

namespace idhan::database
{

namespace internal
{
	struct ConnectionManagerTester;
}

//! Manages connections inside of a single thread.
/**
 * ConnectionManager is responsible for handling how RecursiveConnection works inside of a thread. Where the connection is reserved for that thread only.
 */
class ConnectionManager
{
	friend struct internal::ConnectionManagerTester;

	idhan::database::ConnectionPool pool;

	std::unordered_map< std::thread::id, std::shared_ptr< UniqueConnection > > activeConnections;

	std::mutex connection_lock;

  public:
	ConnectionManager() = delete;

	ConnectionManager( const std::string& connection_str );

	std::shared_ptr< UniqueConnection > acquireShared();

	UniqueConnection acquire();

	void release();
};

namespace internal
{
	struct ConnectionManagerTester
	{
		static auto& pool(ConnectionManager& manager)
		{
			return manager.pool;
		}
	};
}

}

#endif	// IDHAN_CONNECTIONMANAGER_HPP
