#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANDownloader/http/LaneSnapshot.hpp"
#include "LaneSettings.hpp"

namespace idhan::downloader
{

//! Scheduling state that survives transport retirement.
class LanePolicy
{
  public:

	using SteadyClock = std::chrono::steady_clock;
	using SystemClock = std::chrono::system_clock;

  private:

	std::string m_key;
	mutable std::mutex m_mutex {};
	LaneSettings m_settings {};
	LaneSettings m_configured_settings {};
	std::chrono::seconds m_limit_backoff {};
	std::size_t m_successes {};
	bool m_disabled {};
	std::optional< std::uint64_t > m_burst_ceiling {};
	//! Start times of the last m_capacity requests, oldest at m_oldest once full.
	std::vector< SteadyClock::time_point > m_starts {};
	std::size_t m_capacity { 1 };
	std::size_t m_oldest {};
	std::optional< SteadyClock::time_point > m_last_start {};
	SteadyClock::duration m_window {};
	SteadyClock::duration m_error_backoff {};
	//! Failures arriving inside an open pause do not compound it.
	SteadyClock::time_point m_backoff_until {};
	std::uint64_t m_generation {};
	std::string m_advertised {};
	std::uint64_t m_rate_limit_hits {};
	bool m_rate_limited {};
	std::uint64_t m_parser_error_hits {};
	bool m_parser_limited {};

	//! Called with m_mutex held.
	[[nodiscard]] SteadyClock::time_point readyAt() const;
	void configureLocked( LaneSettings settings );

	//! Called with m_mutex held.
	void pause( SteadyClock::duration cooldown, SteadyClock::time_point now );

  public:

	LanePolicy( std::string key, LaneSettings settings );

	//! Host refreshes must not refill the global budget or cancel a server-imposed pause.
	void configure( LaneSettings settings );

	[[nodiscard]] LaneSettings settings() const;

	[[nodiscard]] const std::string& key() const { return m_key; }

	//! Changes when an armed wakeup must be recalculated.
	[[nodiscard]] std::uint64_t generation() const;

	[[nodiscard]] SteadyClock::time_point nextSlot() const;
	[[nodiscard]] bool disabled() const;

	//! Ten consecutive successful responses recover one request per configured window.
	void succeeded( SteadyClock::time_point now = SteadyClock::now() );
	void unsuccessful();

	[[nodiscard]] std::size_t concurrency( std::size_t default_concurrency ) const;

	//! Claims a slot or returns its delay; duration::max() means disabled and must not arm a timer.
	//! Any window holds at most the configured number of claims.
	[[nodiscard]] std::optional< SteadyClock::duration > claim( SteadyClock::time_point now = SteadyClock::now() );

	//! Each 429 lowers the allowance by one, to a floor of one request per window.
	//! Without Retry-After, retry delays double from 1s to 60s. A further failed retry
	//! after that maximum pause disables the lane until reset(). Concurrent failures
	//! during an open pause do not advance the exponential delay.
	//! Parser errors retain their configured cooldown and do not change rate limits.
	SteadyClock::duration limited(
		std::optional< std::string_view > retry_after = std::nullopt,
		SteadyClock::time_point now = SteadyClock::now(),
		SystemClock::time_point wall_time = SystemClock::now(),
		bool parser_error = false );
	void failed( SteadyClock::time_point now = SteadyClock::now() );

	void advertised( std::string headers );

	//! Stops the lane for a set time. Joins a pause that is already open.
	void hold( SteadyClock::duration wait, SteadyClock::time_point now = SteadyClock::now() );

	//! Re-enables the lane, restarts the backoff sequence, and clears the request window.
	void reset();

	void fill( LaneSnapshot& snapshot, SteadyClock::time_point now = SteadyClock::now() ) const;

	[[nodiscard]] static std::optional< std::chrono::seconds > parseRetryAfter(
		std::string_view value,
		SystemClock::time_point now = SystemClock::now() );
};

} // namespace idhan::downloader
