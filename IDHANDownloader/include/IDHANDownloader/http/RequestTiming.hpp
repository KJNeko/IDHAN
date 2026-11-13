#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace idhan::downloader
{

struct RequestTiming
{
	std::chrono::steady_clock::time_point made {};
	std::optional< std::chrono::steady_clock::time_point > queued {};
	std::optional< std::chrono::steady_clock::time_point > sent {};
	//! Wall time of the outgoing request headers, for logging an actual send timestamp.
	std::optional< std::chrono::system_clock::time_point > sent_wall {};
	std::optional< std::chrono::steady_clock::time_point > received {};
	std::uint32_t attempts {};
};

[[nodiscard]] std::string describeRequestTiming( const RequestTiming& timing );

} // namespace idhan::downloader
