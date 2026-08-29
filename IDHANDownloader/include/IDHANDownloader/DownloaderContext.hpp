#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "CookiePersistence.hpp"
#include "ImportSink.hpp"
#include "LaneSnapshot.hpp"
#include "SecretProvider.hpp"
#include "SessionContext.hpp"
#include "SessionSnapshot.hpp"

namespace idhan::downloader
{

struct RequestRate
{
	std::uint64_t requests { 1 };
	std::uint64_t seconds { 5 };

	bool operator==( const RequestRate& ) const = default;
};

//! Unsupported HTTP versions fall back to an available version.
enum class HttpVersion : std::uint8_t
{
	HTTP_1_1,
	HTTP_2,
	HTTP_3,
};

struct DownloaderConfig
{
	std::filesystem::path parser_directory {};
	//! Defaults to url-classes.json inside parser_directory.
	std::filesystem::path url_classes {};

	//! 0 takes max( 2, hardware_concurrency() / 2 ).
	std::size_t io_threads {};
	//! Idle time before a lane closes its connections.
	std::chrono::seconds lane_keep_alive { 30 };
	//! A failed request stops its lane for this long. Zero keeps only the exponential widening.
	std::chrono::seconds lane_error_backoff { 30 };
	//! Only unthrottled lanes grow beyond one shard.
	std::size_t max_shards_per_lane { 4 };
	std::size_t shard_growth_threshold { 8 };
	std::size_t unthrottled_lane_concurrency { 32 };
	std::size_t throttled_lane_concurrency { 4 };

	std::size_t session_inflight_requests { 64 };

	//! Shared JavaScript workers. Each realm remains on its creating worker.
	std::size_t script_threads { 4 };
	//! Memory limit per worker runtime.
	std::size_t worker_memory_limit { 256 * 1024 * 1024 };
	std::size_t worker_stack_limit { 1024 * 1024 };
	//! Limit for one uninterrupted JavaScript run.
	std::chrono::milliseconds script_burst_timeout { 5000 };
	//! In-memory response limit; streamed imports bypass it.
	std::size_t max_response_bytes { 32 * 1024 * 1024 };

	//! Used when neither the URL class nor host specifies requestRate.
	RequestRate default_rate { .requests = 1, .seconds = 5 };
	HttpVersion http_version { HttpVersion::HTTP_3 };
	//! Empty takes the built-in IDHAN user agent.
	std::string user_agent {};
	//! Overrides for the flag defaults a parser declares in its manifest, keyed by flag name.
	std::unordered_map< std::string, bool > flags {};
};

struct DownloaderHost
{
	ImportSinkFactory* imports {};
	CookiePersistence* cookies {};
	SecretProvider* secrets {};
	LaneObserver* lanes {};
};

class DownloaderContext
{
	class Impl;
	std::unique_ptr< Impl > m_impl;

	explicit DownloaderContext( std::unique_ptr< Impl > impl );

  public:

	[[nodiscard]] static std::expected< std::unique_ptr< DownloaderContext >, std::string > create(
		DownloaderConfig config,
		DownloaderHost host );

	DownloaderContext( const DownloaderContext& ) = delete;
	DownloaderContext& operator=( const DownloaderContext& ) = delete;
	~DownloaderContext();

	//! Sessions must be destroyed before their context.
	[[nodiscard]] std::shared_ptr< SessionContext > createSession( SessionOptions options );

	//! Returns an error when the URL is invalid or no URL class accepts it.
	[[nodiscard]] std::expected< void, std::string > validate( std::string_view url ) const;

	//! Backoff persists until explicitly cleared by either reset function.
	void resetBackoff( std::string_view lane_key );
	void resetAllBackoff();

	[[nodiscard]] std::vector< LaneSnapshot > laneSnapshots() const;

	//! Returns live sessions in creation order.
	[[nodiscard]] std::vector< SessionSnapshot > sessionSnapshots( std::uint64_t since = 0 ) const;

	//! Idempotently stops sessions and lanes.
	void shutdown();
};

} // namespace idhan::downloader
