#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "IDHANDownloader/http/RequestRate.hpp"

namespace idhan::downloader
{

struct LaneSnapshot
{
	std::string key {};
	std::string group {};
	RequestRate rate { 0, 0 };
	std::chrono::steady_clock::duration remaining {};
	bool backed_off {};
	bool disabled {};
	std::size_t in_flight {};
	std::size_t concurrency_limit {};
	std::uint64_t dispatched {};
	std::string dispatch_wait { "idle" };
	std::size_t queued {};
	std::uint64_t download_bytes_per_second {};
	//! Last advertised Retry-After and rate-limit headers.
	std::string advertised_limits {};
	bool active {};
	std::uint64_t rate_limit_hits {};
	bool rate_limited {};
	std::uint64_t parser_error_hits {};
	bool parser_limited {};
};

} // namespace idhan::downloader
