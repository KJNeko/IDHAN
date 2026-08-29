#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "IDHANDownloader/LaneSnapshot.hpp"
#include "http/Transfer.hpp"

namespace idhan::downloader
{
class IoPool;
class IoThread;
class LanePolicy;
class LaneShard;

//! Connection owner for one host, kept alive by in-flight callbacks.
class Lane : public std::enable_shared_from_this< Lane >
{
  public:

	struct Config
	{
		std::size_t max_shards { 4 };
		std::size_t shard_growth_threshold { 8 };
		std::size_t unthrottled_concurrency { 32 };
		std::size_t throttled_concurrency { 4 };
	};

  private:

	struct Pending
	{
		TransferRequest request {};
		TransferCallback callback {};
	};

	//! Identifies the policy generation behind the current queue wakeup.
	struct Gate
	{
		bool armed {};
		std::chrono::steady_clock::time_point deadline {};
		std::uint64_t generation {};
	};

	std::string m_key;
	LanePolicy& m_policy;
	IoPool& m_pool;
	//! All wakeups use one event loop to preserve their order.
	IoThread& m_timer_thread;
	Config m_config;

	mutable std::mutex m_mutex {};
	std::deque< Pending > m_queue {};
	std::vector< std::shared_ptr< LaneShard > > m_shards {};
	std::size_t m_in_flight {};
	std::chrono::steady_clock::time_point m_last_enqueued { std::chrono::steady_clock::now() };
	Gate m_gate {};
	bool m_stopped {};

	static void pump( const std::shared_ptr< Lane >& lane );
	//! Called with m_mutex held.
	void growIfNeeded();
	[[nodiscard]] std::shared_ptr< LaneShard > pickShard();
	void arm( const std::shared_ptr< Lane >& lane, std::chrono::steady_clock::duration wait );

  public:

	Lane( std::string key, LanePolicy& policy, IoPool& pool, Config config );
	Lane( const Lane& ) = delete;
	Lane& operator=( const Lane& ) = delete;
	~Lane();

	[[nodiscard]] const std::string& key() const { return m_key; }

	void submit( TransferRequest request, TransferCallback callback );
	void cancel( const std::shared_ptr< std::atomic_bool >& cancellation );
	//! Recalculates an armed wakeup after an external policy change.
	void wake();
	void onTransferFinished();

	[[nodiscard]] bool retirable(
		std::chrono::steady_clock::duration keep_alive,
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now() ) const;

	void fill( LaneSnapshot& snapshot ) const;

	//! Fails all transfers and releases shards on their IO threads.
	void shutdown();
};

} // namespace idhan::downloader
