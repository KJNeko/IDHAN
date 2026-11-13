#pragma once

#include <folly/coro/Mutex.h>

#include <folly/coro/AsyncScope.h>
#include <folly/executors/CPUThreadPoolExecutor.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "IDHANDownloader/http/LaneObserver.hpp"
#include "http/IoPool.hpp"
#include "http/Lane.hpp"
#include "http/LanePolicy.hpp"

namespace idhan::downloader
{

class LanePool
{
	struct Exchange;

  public:

	struct Config
	{
		Lane::Config lane {};
		std::chrono::seconds keep_alive { 30 };
		RequestRate default_rate {};
		HttpVersion http_version { HttpVersion::HTTP_3 };
		std::string user_agent {};
		std::size_t max_response_bytes {};
		long timeout_ms {};
		LaneObserver* observer {};
		std::size_t import_threads { 4 };
	};

	using SettingsResolver = std::function< LaneSettings( std::string_view host ) >;

  private:

	Config m_config;
	SettingsResolver m_resolver;
	IoPool m_pool;
	folly::CPUThreadPoolExecutor m_import_executor;
	folly::CPUThreadPoolExecutor m_write_executor;
	folly::coro::AsyncScope m_import_scope;

	mutable std::mutex m_mutex {};
	folly::coro::Mutex m_shutdown_mutex {};
	std::unordered_map< std::string, std::unique_ptr< LanePolicy > > m_policies {};
	std::unordered_map< std::string, std::shared_ptr< Lane > > m_lanes {};
	bool m_stopped {};
	bool m_sweep_armed {};

	[[nodiscard]] std::shared_ptr< Lane > laneFor( const std::string& key, std::string_view host );
	[[nodiscard]] LanePolicy* policyFor( const std::string& key );

	[[nodiscard]] std::string keyFor( std::string_view host ) const;
	void publish( const std::string& key );
	void reportRateLimitHeaders( const std::string& key, LanePolicy* policy, const HttpHeaders& headers );
	void dispatch( const std::shared_ptr< Exchange >& exchange );
	void applyResponseBackoff( const Exchange& exchange, const std::string& key, const TransferResponse& response );
	void dispatchCompletion(
		const std::shared_ptr< Exchange >& exchange,
		const std::string& key,
		TransferResult result );
	folly::coro::Task< void > complete( std::shared_ptr< Exchange > exchange,
		std::string key,
		TransferResult result );
	folly::coro::Task< void > finishImport(
		std::shared_ptr< Exchange > exchange,
		std::string key,
		TransferResult result );
	void armSweep();
	void sweep();

  public:

	LanePool( Config config, SettingsResolver resolver, std::size_t io_threads );
	LanePool( const LanePool& ) = delete;
	LanePool& operator=( const LanePool& ) = delete;
	~LanePool();

	void send( TransferRequest request, TransferCallback callback );
	void cancel( const std::shared_ptr< std::atomic_bool >& cancellation );
	// Parser failures pause the shared lane without changing its request rate.
	void parserFailed( std::string_view url );

	//! Re-reads a host's settings into its lane, for a host whose settings changed after its lane was created.
	void reconfigure( std::string_view host );

	void resetBackoff( std::string_view key );
	void resetAllBackoff();

	[[nodiscard]] std::vector< LaneSnapshot > snapshots() const;

	[[nodiscard]] std::string laneKeyForUrl( std::string_view url ) const;

	folly::coro::Task< void > shutdown();
};

} // namespace idhan::downloader
