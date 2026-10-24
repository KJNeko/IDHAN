#pragma once
#include <folly/coro/Baton.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ValidatedJson.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::http
{
class SSEEventHub;

struct SSEEvent
{
	enum class Kind
	{
		snapshot,
		update,
		//! Written as an SSE comment purely to hold the connection open past the session's idle
		//! timeout. Carries no payload and never surfaces to a client as an event.
		keepalive,
		shutdown
	};
	Kind kind;
	std::string entity;
	std::optional< ValidatedJson > data;
	std::uint64_t sequence;
	//! Seconds since the Unix epoch, taken when the event was generated.
	std::int64_t timestamp;
};

struct SSESubscriber
{
	std::vector< std::string > entities;
	std::vector< SSEEvent > pending;
	bool closed { false };
	folly::coro::Baton readable;

	SSESubscriber() noexcept : entities {}, pending {}, closed { false }, readable {} {}
};

class SSESubscription
{
	std::shared_ptr< SSESubscriber > m_subscriber;
	SSEEventHub* m_hub {};
	friend class SSEEventHub;
	explicit SSESubscription( SSEEventHub&, std::shared_ptr< SSESubscriber > );

  public:

	SSESubscription( SSESubscription&& ) noexcept;
	SSESubscription& operator=( SSESubscription&& ) noexcept;
	SSESubscription( const SSESubscription& ) = delete;
	SSESubscription& operator=( const SSESubscription& ) = delete;
	~SSESubscription();
	[[nodiscard]] runtime::Task< std::optional< SSEEvent > > next();
	void close() noexcept;
};

class SSEEventHub
{
	friend class SSESubscription;
	std::mutex m_mutex;
	std::vector< std::shared_ptr< SSESubscriber > > m_subscribers;
	std::unordered_map< std::string, ValidatedJson > m_snapshots;
	std::uint64_t m_sequence { 0 };
	bool m_shutdown { false };

  public:

	SSEEventHub();
	~SSEEventHub();
	[[nodiscard]] SSESubscription subscribe(
		std::vector< std::string > entities,
		std::vector< ValidatedJson > initial_snapshots = {},
		bool include_stored_snapshots = true );
	void publish( std::string entity, ValidatedJson snapshot );
	void remove( std::string entity, std::optional< ValidatedJson > removal = {} );
	void heartbeat();
	//! Keeps every hub's streams alive, not just the one the caller happens to hold. An SSE stream
	//! that sends nothing is closed by the session's idle timeout, and the client then has to
	//! reconnect to resync.
	static void heartbeatAll();
	void shutdown();
};
} // namespace idhan::http
