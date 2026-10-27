#pragma once
#include <folly/Executor.h>
#include <folly/coro/BoundedQueue.h>

#include <memory>
#include <mutex>
#include <vector>

#include "DBConnection.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBTransaction;

class DBPool
{
	DBConfig m_config;
	std::shared_ptr< DBReactor > m_reactor;
	mutable std::mutex m_mutex {};
	std::vector< std::shared_ptr< DBConnection > > m_normal {};
	std::vector< std::shared_ptr< DBConnection > > m_transactions {};
	std::shared_ptr< folly::coro::BoundedQueue< std::shared_ptr< DBConnection > > > m_available_transactions {};
	folly::Executor::KeepAlive<> m_application_executor;
	std::size_t m_next_tie { 0 };
	bool m_starting { false };
	bool m_started { false };
	bool m_draining { false };

  public:

	DBPool( DBConfig, std::shared_ptr< DBReactor >, folly::Executor::KeepAlive<> );
	~DBPool();
	[[nodiscard]] runtime::Task<> start();
	void submit( std::shared_ptr< DBOperation > );
	[[nodiscard]] runtime::Task< DBTransaction > transaction();
	[[nodiscard]] std::vector< ConnectionStats > stats() const;
	[[nodiscard]] runtime::Task<> drain();
};
} // namespace idhan::db
