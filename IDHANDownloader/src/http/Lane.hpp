#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "IDHANDownloader/http/DownloadSpeed.hpp"
#include "IDHANDownloader/http/LaneSnapshot.hpp"
#include "http/Transfer.hpp"

namespace idhan::downloader
{
class IoPool;
class IoThread;
class LanePolicy;
class LaneShard;
class LaneObserver;

//! Connection owner for one host, kept alive by in-flight callbacks.
class Lane : public std::enable_shared_from_this< Lane >
{
  public:

	struct Config
	{
		std::size_t concurrency { 1 };
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
	LaneObserver* m_observer {};

	mutable std::mutex m_mutex {};
	std::deque< Pending > m_queue {};
	// File imports drain first; both queues preserve submission order within their priority.
	std::deque< Pending > m_import_queue {};
	std::shared_ptr< LaneShard > m_shard {};
	std::size_t m_in_flight {};
	std::uint64_t m_dispatched {};
	std::chrono::steady_clock::time_point m_last_enqueued { std::chrono::steady_clock::now() };
	Gate m_gate {};
	std::uint64_t m_downloaded_bytes {};
	mutable DownloadSpeed m_download_speed;
	bool m_stopped {};

	static void pump( const std::shared_ptr< Lane >& lane );
	void publish() const;
	void arm( const std::shared_ptr< Lane >& lane, std::chrono::steady_clock::duration wait );

  public:

	Lane( std::string key, LanePolicy& policy, IoPool& pool, Config config, LaneObserver* observer = nullptr );
	Lane( const Lane& ) = delete;
	Lane& operator=( const Lane& ) = delete;
	~Lane();

	[[nodiscard]] const std::string& key() const { return m_key; }

	void submit( TransferRequest request, TransferCallback callback );
	void cancel( const std::shared_ptr< std::atomic_bool >& cancellation );
	//! Recalculates an armed wakeup after an external policy change.
	void wake();
	void onTransferFinished();
	void onTransferFinalizing();
	void onBytesReceived( std::size_t bytes );

	[[nodiscard]] bool retirable(
		std::chrono::steady_clock::duration keep_alive,
		std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now() ) const;

	void fill( LaneSnapshot& snapshot, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now() )
		const;

	//! Fails all transfers and releases its connection pool on its IO thread.
	void shutdown();
};

} // namespace idhan::downloader
