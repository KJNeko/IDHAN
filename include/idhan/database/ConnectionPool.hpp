//
// Created by kj16609 on 9/3/22.
//

#pragma once
#ifndef IDHAN_CONNECTIONPOOL_HPP
#define IDHAN_CONNECTIONPOOL_HPP

#include <queue>
#include <memory>
#include <atomic>
#include <semaphore>

//Box

#include <pqxx/pqxx>

namespace idhan::database
{

namespace internal {
	struct ConnectionPoolTester;
}


class ConnectionPool
{
	friend class idhan::database::internal::ConnectionPoolTester;

	static constexpr uint64_t CONNECTION_MAX_COUNT {32};

	//TODO: Replace queue with ring buffer
	std::queue<std::unique_ptr<pqxx::connection>> connections {};

	std::counting_semaphore<CONNECTION_MAX_COUNT> ready_connections{0};

	std::mutex pool_lock;

	std::atomic<bool> initalized {false};

  public:

	ConnectionPool() = default;

	ConnectionPool(const std::string& conn_args);

	pqxx::connection* acquire();

	void release(pqxx::connection* conn);

	void init(const std::string& conn_string);

	void deinit();

	~ConnectionPool();
};

namespace internal
{
	struct ConnectionPoolTester
	{
		static auto& connections(const ConnectionPool& pool)
		{
			return pool.connections;
		}
	};



}

}
#endif	// IDHAN_CONNECTIONPOOL_HPP
