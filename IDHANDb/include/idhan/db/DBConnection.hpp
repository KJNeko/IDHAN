#pragma once
#include <folly/coro/Baton.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <libpq-fe.h>
#include <memory>
#include <mutex>

#include "DBConfig.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBOperation;
class DBReactor;
enum class ConnectionState : std::uint8_t
{
	cold,
	connecting,
	configuring,
	connected,
	failed
};

enum class ConnectionRole
{
	normal,
	transaction,
	management
};

struct ConnectionStats
{
	std::uint64_t id;
	std::size_t queued, sent, unfinished;
	bool input_paused;
};

class DBConnection : public std::enable_shared_from_this< DBConnection >
{
	const std::uint64_t id;
	const ConnectionRole role;
	const DBConfig config;

	DBReactor& reactor;
	PGconn* connection { nullptr };

	std::atomic< ConnectionState > state { ConnectionState::cold };
	folly::coro::Baton connected {};
	std::exception_ptr connect_error {};
	bool saw_startup_sync { false };
	mutable std::mutex queue_mutex {};

	std::deque< std::shared_ptr< DBOperation > > queued {};
	std::deque< std::shared_ptr< DBOperation > > inflight {};

	std::size_t sent { 0 };
	bool input_paused { false };
	folly::coro::Baton drained { true };

	[[nodiscard]] static runtime::Task<> connectImpl( std::shared_ptr< DBConnection > );
	[[nodiscard]] static runtime::Task<> drainImpl( std::shared_ptr< DBConnection > );

	void pumpConnected();
	void pumpHandshake( ConnectionState );

	[[nodiscard]] std::shared_ptr< DBOperation > popDispatchable( bool use_pipeline );

	[[nodiscard]] std::shared_ptr< DBOperation > getHead() const;

	void pushInflight( std::shared_ptr< DBOperation > operation );

	void retireHead();

	void postDrainedIfIdle();
	void logFirstResult( const DBOperation& ) const;
	void logCompletion( const DBOperation& ) const;

	[[nodiscard]] bool hasWork() const;

  public:

	DBConnection( std::uint64_t id, ConnectionRole, DBConfig, DBReactor& );
	~DBConnection();
	DBConnection( const DBConnection& ) = delete;
	DBConnection& operator=( const DBConnection& ) = delete;
	[[nodiscard]] runtime::Task<> connect();
	void enqueue( std::shared_ptr< DBOperation > );
	void wake();
	// Only the serialized pump may operate on its PGconn.
	void pump();
	[[nodiscard]] ConnectionStats stats() const;
	[[nodiscard]] runtime::Task<> drain();
	[[nodiscard]] runtime::Task<> restoreLease();
};

} // namespace idhan::db
