#pragma once
#include <folly/coro/Mutex.h>

#include <mutex>

#include "idhan/db/DBTransaction.hpp"

namespace idhan::filesystem
{
class ClusterManager;

// Owns the writer lock and transaction. Unfinished work rolls back on destruction;
// only a successful commit publishes the new configuration snapshot.
class ClusterWrite
{
	friend class ClusterManager;
	ClusterManager& m_manager;
	std::unique_lock< folly::coro::Mutex > m_lock;
	db::DBTransaction m_transaction;
	ClusterWrite( ClusterManager& manager, std::unique_lock< folly::coro::Mutex > lock, db::DBTransaction transaction );

  public:

	ClusterWrite( ClusterWrite&& ) noexcept = default;

	// Intentional borrowed access for transaction-local validation and writes.
	[[nodiscard]] db::DBTransaction& transaction() { return m_transaction; }

	[[nodiscard]] runtime::Task<> commit();
	[[nodiscard]] runtime::Task<> rollback();
};
} // namespace idhan::filesystem
