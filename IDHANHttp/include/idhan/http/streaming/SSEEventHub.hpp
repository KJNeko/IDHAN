#pragma once
#include <folly/coro/Baton.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "idhan/http/json/ValidatedJson.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::http
{
class SSEEventHub;

//! The stream an event type belongs to: the text before its first '.', written as the SSE `event:` name.
[[nodiscard]] std::string_view streamOf( std::string_view type );

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
	//! Internal key such as `downloader.url_updated`; only its stream reaches the wire.
	std::string type;
	std::string entity;
	std::optional< ValidatedJson > data;
	std::uint64_t sequence;
	//! Set for events that belong to one download session and honour a subscriber's session filter.
	std::optional< std::string > session {};
};

struct SSEInitialEvent
{
	std::string type;
	std::string entity;
	ValidatedJson data;
	std::optional< std::string > session {};
};

struct SSEPublishOptions
{
	bool retain_snapshot { true };
	//! Replaces a queued update with the same type and entity instead of appending.
	bool coalesce { true };
	std::optional< std::string > session {};
};

struct SSESubscriber
{
	std::vector< std::string > streams;
	std::vector< std::string > session_ids;
	std::vector< SSEEvent > pending;
	//! Payload bytes of the queued updates; snapshots are excluded so a large baseline cannot trip the cap.
	std::size_t pending_update_bytes { 0 };
	bool closed { false };
	folly::coro::Baton readable;

	SSESubscriber() noexcept :
	  streams {},
	  session_ids {},
	  pending {},
	  pending_update_bytes { 0 },
	  closed { false },
	  readable {}
	{}
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
	[[nodiscard]] std::uint64_t checkpoint();
	//! Drops updates covered by the baseline through checkpoint: the specified stream, or session-scoped updates by default.
	void prependSnapshots(
		std::vector< SSEInitialEvent > snapshots,
		std::uint64_t checkpoint,
		std::optional< std::string_view > replaced_stream = std::nullopt );
};

class SSEEventHub
{
	friend class SSESubscription;

	struct StoredSnapshot
	{
		ValidatedJson data;
		std::optional< std::string > session {};
	};

	std::mutex m_mutex;
	std::vector< std::shared_ptr< SSESubscriber > > m_subscribers;
	std::unordered_map< std::string, StoredSnapshot > m_snapshots;
	std::uint64_t m_sequence { 0 };
	bool m_shutdown { false };

	void enqueue( SSESubscriber& subscriber, SSEEvent event, bool coalesce );

  public:

	//! A subscriber whose queued updates exceed this is disconnected and must reconnect for a new snapshot.
	static constexpr std::size_t max_pending_update_bytes { 8 * 1024 * 1024 };

	SSEEventHub();
	~SSEEventHub();
	//! Empty streams receive every stream. Empty session IDs receive every session.
	[[nodiscard]] SSESubscription subscribeEvents(
		std::vector< std::string > streams,
		std::vector< SSEInitialEvent > initial_events = {},
		bool include_stored_snapshots = true,
		std::vector< std::string > session_ids = {} );
	void publish( std::string type, std::string entity, ValidatedJson data, SSEPublishOptions options = {} );
	void remove( std::string type, std::string entity, std::optional< ValidatedJson > removal = {} );
	[[nodiscard]] bool interested( std::string_view type, std::optional< std::string_view > session = {} );
	void heartbeat();

	//! Keeps every hub's streams alive so idle SSE connections survive the session's idle timeout.
	static void heartbeatAll();
	void shutdown();
};
} // namespace idhan::http
