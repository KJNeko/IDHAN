#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace idhan::downloader
{

struct LaneSnapshot
{
	std::string key {};
	std::optional< std::string > group {};
	std::uint64_t rate_requests {};
	std::uint64_t rate_seconds {};
	bool throttled {};
	std::chrono::steady_clock::duration effective_interval {};
	std::chrono::steady_clock::duration remaining {};
	std::uint32_t consecutive_failures {};
	bool backed_off {};
	std::size_t in_flight {};
	std::size_t queued {};
	std::size_t shards {};
	std::uint64_t bytes_per_second {};
	//! Last advertised Retry-After and rate-limit headers.
	std::string advertised_limits {};
	bool active {};
};

//! Called from downloader IO threads; implementations must be thread-safe and quick.
class LaneObserver
{
  public:

	virtual ~LaneObserver() = default;

	virtual void onLaneChanged( const LaneSnapshot& snapshot ) = 0;
};

} // namespace idhan::downloader
