#pragma once
#include <folly/Executor.h>

#include <coroutine>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "DBBindings.hpp"
#include "DBResult.hpp"

namespace idhan::db
{
class DBConnection;
enum class OperationPhase
{
	cold,
	queued,
	sent,
	streaming,
	synchronizing,
	completed,
	abandoned
};

class DBOperation : public std::enable_shared_from_this< DBOperation >
{
	const std::uint64_t m_id;
	const std::string m_sql;
	const DBBindings m_bindings;
	const QueryOptions m_options;
	const folly::Executor::KeepAlive<> m_application_executor;
	mutable std::mutex m_mutex;
	OperationPhase m_phase { OperationPhase::cold };
	std::function< void( std::shared_ptr< DBOperation > ) > m_starter;
	std::coroutine_handle<> m_continuation {};
	DBResult m_result {};
	std::exception_ptr m_error;
	std::optional< std::chrono::steady_clock::time_point > m_sent_at;
	std::optional< std::chrono::steady_clock::time_point > m_first_result_at;
	std::optional< std::chrono::steady_clock::time_point > m_completed_at;
	bool m_consumer_cancelled { false };
	friend class DBConnection;
	friend class DBTask;

	void start();
	void markQueued();
	[[nodiscard]] bool send( PGconn*, bool pipeline );
	void acceptResult( DBResult );
	void acceptFailure( std::exception_ptr );
	void complete();
	void fail( std::exception_ptr );
	[[nodiscard]] DBResult takeResult();
	void rethrowIfFailed() const;
	[[nodiscard]] bool completed() const noexcept;
	[[nodiscard]] bool isSent() const noexcept;
	[[nodiscard]] bool needsSynchronization() const noexcept;
	[[nodiscard]] const DBBindings& bindings() const noexcept;
	[[nodiscard]] const QueryOptions& options() const noexcept;
	[[nodiscard]] const std::string& sql() const noexcept;
	[[nodiscard]] std::optional< std::chrono::milliseconds > firstResultLatency() const;
	[[nodiscard]] std::optional< std::chrono::milliseconds > completionLatency() const;

  public:

	DBOperation( std::string sql, DBBindings, QueryOptions, folly::Executor::KeepAlive<> );
	~DBOperation();
	DBOperation( const DBOperation& ) = delete;
	DBOperation& operator=( const DBOperation& ) = delete;
	[[nodiscard]] OperationPhase phase() const noexcept;
	[[nodiscard]] std::uint64_t id() const noexcept;
	// The owning pool installs this before exposing a cold DBTask.
	void setStarter( std::function< void( std::shared_ptr< DBOperation > ) > );
	void abandon() noexcept;
	void consumerAdvanced();
	// Cleanup ownership persists independently of the consumer coroutine.
	void registerContinuation( std::coroutine_handle<> );
	void unregisterContinuation() noexcept;
};
} // namespace idhan::db
