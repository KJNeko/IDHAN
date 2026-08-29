#pragma once

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
	};

	using SettingsResolver = std::function< LaneSettings( std::string_view host ) >;

  private:

	Config m_config;
	SettingsResolver m_resolver;
	IoPool m_pool;

	mutable std::mutex m_mutex {};
	std::unordered_map< std::string, std::unique_ptr< LanePolicy > > m_policies {};
	std::unordered_map< std::string, std::shared_ptr< Lane > > m_lanes {};
	bool m_stopped {};
	bool m_sweep_armed {};

	[[nodiscard]] std::shared_ptr< Lane > laneFor( const std::string& key, std::string_view host );
	[[nodiscard]] LanePolicy* policyFor( const std::string& key );

	[[nodiscard]] std::string keyFor( std::string_view host ) const;
	void publish( const std::string& key );
	void reportThrottleHeaders( const std::string& key, LanePolicy* policy, const HttpHeaders& headers );
	void dispatch( const std::shared_ptr< Exchange >& exchange );
	void complete( const std::shared_ptr< Exchange >& exchange, const std::string& key, TransferResult result );
	void armSweep();
	void sweep();

  public:

	LanePool( Config config, SettingsResolver resolver, std::size_t io_threads );
	LanePool( const LanePool& ) = delete;
	LanePool& operator=( const LanePool& ) = delete;
	~LanePool();

	void send( TransferRequest request, TransferCallback callback );
	void cancel( const std::shared_ptr< std::atomic_bool >& cancellation );

	void resetBackoff( std::string_view key );
	void resetAllBackoff();

	[[nodiscard]] std::vector< LaneSnapshot > snapshots() const;

	[[nodiscard]] std::string laneKeyForUrl( std::string_view url ) const;

	void shutdown();
};

} // namespace idhan::downloader
