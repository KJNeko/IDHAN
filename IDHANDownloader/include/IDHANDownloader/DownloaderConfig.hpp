#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "HttpVersion.hpp"
#include "RequestRate.hpp"

namespace idhan::downloader
{

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

} // namespace idhan::downloader
