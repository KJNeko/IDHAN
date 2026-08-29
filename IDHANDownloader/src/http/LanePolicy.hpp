#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "IDHANDownloader/DownloaderContext.hpp"
#include "IDHANDownloader/LaneSnapshot.hpp"

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
	SteadyClock::duration m_base_interval {};
	SteadyClock::duration m_interval {};
	SteadyClock::duration m_maximum_interval;
	SteadyClock::duration m_error_backoff {};
	SteadyClock::time_point m_next_request {};
	//! Failures arriving inside an open pause do not compound it.
	SteadyClock::time_point m_backoff_until {};
	std::uint32_t m_consecutive_failures {};
	std::uint64_t m_generation {};
	std::string m_advertised {};

	void widen();
	//! Widens once per pause and reports the cooldown a fresh failure earns.
	[[nodiscard]] SteadyClock::duration openPause( SteadyClock::time_point now );
	void pause( SteadyClock::duration cooldown, SteadyClock::time_point now );

  public:

	LanePolicy(
		std::string key,
		LaneSettings settings,
		SteadyClock::duration maximum_interval = std::chrono::hours { 1 } );

	//! Reconfigures the lane and clears its backoff.
	void configure( LaneSettings settings );

	[[nodiscard]] LaneSettings settings() const;

	[[nodiscard]] const std::string& key() const { return m_key; }

	//! Changes when an armed wakeup must be recalculated.
	[[nodiscard]] std::uint64_t generation() const;

	[[nodiscard]] SteadyClock::time_point nextSlot() const;

	[[nodiscard]] bool throttled() const;
	[[nodiscard]] std::size_t concurrency( std::size_t unthrottled_default, std::size_t throttled_default ) const;

	//! Claims a slot or returns its delay. Late claims do not shift the schedule.
	[[nodiscard]] std::optional< SteadyClock::duration > claim( SteadyClock::time_point now = SteadyClock::now() );

	//! Stops the lane like failed(). A Retry-After sets the pause, floored by the configured interval.
	SteadyClock::duration limited(
		std::optional< std::string_view > retry_after = std::nullopt,
		SteadyClock::time_point now = SteadyClock::now(),
		SystemClock::time_point wall_time = SystemClock::now() );
	//! Stops the lane for the configured backoff, widening it once per pause.
	void failed( SteadyClock::time_point now = SteadyClock::now() );

	void advertised( std::string headers );

	//! Clears backoff; successful requests do not.
	void reset();

	void fill( LaneSnapshot& snapshot, SteadyClock::time_point now = SteadyClock::now() ) const;

	[[nodiscard]] static std::optional< std::chrono::seconds > parseRetryAfter(
		std::string_view value,
		SystemClock::time_point now = SystemClock::now() );
};

} // namespace idhan::downloader
