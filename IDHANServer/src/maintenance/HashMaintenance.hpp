#pragma once

#include <folly/Executor.h>

#include <thread>

namespace idhan::db
{
class DBContext;
}

namespace idhan::maintenance
{
// Owns timer producers and drains their in-flight batches before dependencies
// can be destroyed. Each producer has at most one sweep in flight.
class HashMaintenance
{
	db::DBContext& m_db;
	folly::Executor::KeepAlive<> m_executor;
	std::jthread m_hamming;
	std::jthread m_phash;

	void runHamming( std::stop_token stop );
	void runPhash( std::stop_token stop );

  public:

	HashMaintenance( db::DBContext& db, folly::Executor::KeepAlive<> executor );
	~HashMaintenance();
	HashMaintenance( const HashMaintenance& ) = delete;
	HashMaintenance& operator=( const HashMaintenance& ) = delete;
	void stop();
};
} // namespace idhan::maintenance
