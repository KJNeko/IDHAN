#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

namespace idhan::downloader
{

//! Diagnostics threads read these counters without accessing QuickJS.
class ScriptActivity
{
  public:

	struct Snapshot
	{
		bool executing {};
		std::uint64_t busy_ns {};
		std::uint64_t bursts {};
	};

  private:

	mutable std::mutex m_mutex {};
	Snapshot m_state {};
	std::chrono::steady_clock::time_point m_started {};

  public:

	ScriptActivity() noexcept = default;

	void enter()
	{
		const std::scoped_lock lock { m_mutex };
		m_started = std::chrono::steady_clock::now();
		m_state.executing = true;
		++m_state.bursts;
	}

	void leave()
	{
		const std::scoped_lock lock { m_mutex };
		m_state.busy_ns += static_cast< std::uint64_t >(
			std::chrono::duration_cast< std::chrono::nanoseconds >( std::chrono::steady_clock::now() - m_started )
				.count() );
		m_state.executing = false;
	}

	void resume()
	{
		const std::scoped_lock lock { m_mutex };
		m_started = std::chrono::steady_clock::now();
		m_state.executing = true;
	}

	[[nodiscard]] Snapshot snapshot() const
	{
		const std::scoped_lock lock { m_mutex };
		Snapshot state { m_state };
		if ( state.executing )
			state.busy_ns += static_cast< std::uint64_t >(
				std::chrono::duration_cast< std::chrono::nanoseconds >( std::chrono::steady_clock::now() - m_started )
					.count() );

		return state;
	}
};

} // namespace idhan::downloader
