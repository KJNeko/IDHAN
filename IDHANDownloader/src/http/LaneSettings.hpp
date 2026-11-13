#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "IDHANDownloader/http/HttpVersion.hpp"
#include "IDHANDownloader/http/RequestRate.hpp"

namespace idhan::downloader
{

struct StatusRetry
{
	std::uint32_t count {};
	//! Lane-wide pause before each attempt, for hosts that signal rate limits with other statuses.
	std::chrono::seconds wait {};
};

struct LaneSettings
{
	//! No rate means request starts are unlimited.
	std::optional< RequestRate > rate {};
	std::chrono::seconds keep_alive { 30 };
	//! Lane-wide pause for backoff_statuses and parser errors. HTTP 429 uses a separate exponential delay.
	std::chrono::seconds error_backoff { 30 };
	//! 0 selects the default lane concurrency.
	std::size_t concurrency {};
	std::optional< HttpVersion > http_version {};
	std::optional< std::string > group {};
	//! Additional attempts per HTTP status, inherited from the URL class unless a host overrides them.
	std::unordered_map< std::int32_t, StatusRetry > status_retries {};
	//! Selected statuses pause the lane. A 429 always does, selected or not.
	std::unordered_set< std::int32_t > backoff_statuses {};
};

} // namespace idhan::downloader
