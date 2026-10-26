#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "IDHANDownloader/HttpVersion.hpp"
#include "IDHANDownloader/RequestRate.hpp"

namespace idhan::downloader
{

struct LaneSettings
{
	//! No rate means unthrottled.
	std::optional< RequestRate > rate {};
	std::uint64_t bytes_per_second {};
	std::chrono::seconds keep_alive { 30 };
	//! Lane-wide pause after a request fails. Zero keeps only the exponential widening.
	std::chrono::seconds error_backoff { 30 };
	//! 0 selects the default for the lane's throttle state.
	std::size_t concurrency {};
	std::optional< HttpVersion > http_version {};
	std::optional< std::string > group {};
};

} // namespace idhan::downloader
