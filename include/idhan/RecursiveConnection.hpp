//
// Created by kj16609 on 9/4/22.
//

#ifndef IDHAN_RECURSIVECONNECTION_HPP
#define IDHAN_RECURSIVECONNECTION_HPP

#include <semaphore>
#include <queue>

#include <pqxx/pqxx>

#define IDHAN_CONNECTION_COUNT 16

namespace idhan::database
{

class ConnectionPool
{
	friend class Connection;

	inline static std::counting_semaphore< IDHAN_CONNECTION_COUNT > connection_counter{ 0 };

	inline static std::queue< pqxx::connection* > connections;
	inline static std::mutex conn_lock;

	inline static bool ready{false};

	static pqxx::connection* acquire()
	{
		if(!ready)
		{
			throw std::runtime_error("ConnectionPool not ready when Connection attempted to initalize");
		}

		connection_counter.acquire();
		std::lock_guard< std::mutex > guard( conn_lock );
		auto* ptr = connections.front();
		connections.pop();

		return ptr;
	}
  public:
	static void release( pqxx::connection* conn_ptr )
	{
		std::lock_guard< std::mutex > guard( conn_lock );
		connections.push( conn_ptr );
		connection_counter.release();
	}

	static void init(const std::string& db_conn_str)
	{
		if(ready)
		{
			throw std::runtime_error("Attempted to ConnectionPool::init() twice");
		}

		for(size_t i = 0; i < IDHAN_CONNECTION_COUNT; ++i)
		{
			release(new pqxx::connection(db_conn_str));
		}

		ready = true;
	}

	static void deinit()
	{
		if(!ready)
		{
			throw std::runtime_error("Attempted to ConnectionPool::deinit() while ConnectionPool is not ready (init)");
		}

		for(size_t i = 0; i < IDHAN_CONNECTION_COUNT; ++i)
		{
			delete acquire();
		}

		ready = false;
	}

};

class Connection
{
	pqxx::connection* conn_ptr;

  public:
	Connection() : conn_ptr( ConnectionPool::acquire() ) {}

	~Connection() { ConnectionPool::release( conn_ptr ); }

	pqxx::connection& connection() { return *conn_ptr; }
};
}
#endif	// IDHAN_RECURSIVECONNECTION_HPP
