#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <json/value.h>

#include "IDHANDownloader/http/HttpVersion.hpp"
#include "IDHANDownloader/http/RequestRate.hpp"

namespace idhan::downloader
{

struct DownloaderConfig
{
	bool force_download {};
	std::filesystem::path parser_directory {};
	bool watch_scripts { true };
	std::chrono::milliseconds script_watch_interval { 500 };
	std::chrono::milliseconds script_watch_debounce { 250 };
	//! Optional extra URL class JSON file or directory; parsers supply their own classes.
	std::filesystem::path url_classes {};

	//! 0 takes max( 2, hardware_concurrency() / 2 ).
	std::size_t io_threads {};
	std::chrono::seconds lane_keep_alive { 30 };
	//! Pause for parser errors and statuses in lane.backoffStatuses; 429 uses exponential backoff.
	std::chrono::seconds lane_error_backoff { 30 };
	//! Occupied transfers per lane, including import finalization.
	std::size_t lane_concurrency { 1 };

	std::size_t session_inflight_requests { 64 };

	//! Global runtime instances per parser, created lazily. Each owns a dedicated thread.
	std::size_t parser_instances { 1 };
	std::size_t import_threads { 4 };
	std::size_t worker_memory_limit { 256 * 1024 * 1024 };
	std::size_t worker_stack_limit { 1024 * 1024 };
	//! Limit for one uninterrupted JavaScript run, excluding synchronous host callbacks.
	std::chrono::milliseconds script_burst_timeout { 5000 };
	//! In-memory response limit; streamed imports bypass it.
	std::size_t max_response_bytes { 32 * 1024 * 1024 };

	//! Used when neither the URL class nor host specifies requestRate.
	RequestRate default_rate { .requests = 1, .seconds = 5 };
	HttpVersion http_version { HttpVersion::HTTP_3 };
	//! Empty takes the built-in IDHAN user agent.
	std::string user_agent {};
	//! Per-parser option overrides, keyed by parser ID.
	Json::Value options { Json::objectValue };
};

} // namespace idhan::downloader
