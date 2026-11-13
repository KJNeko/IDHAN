#pragma once

#include <chrono>
#include <cstdint>

namespace idhan::downloader
{
//! Samples a cumulative byte counter no more than once per second. Callers
//! synchronize access and use a steady clock, independent of wall-clock changes.
class DownloadSpeed
{
	std::chrono::steady_clock::time_point m_at;
	std::uint64_t m_previous {};
	std::uint64_t m_speed {};

  public:

	explicit DownloadSpeed( const std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now() ) :
	  m_at( at )
	{}

	std::uint64_t sample(
		const std::uint64_t total,
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now() )
	{
		if ( now < m_at || total < m_previous )
		{
			m_at = now;
			m_previous = total;
			m_speed = 0;
			return m_speed;
		}

		const auto elapsed { now - m_at };
		if ( elapsed < std::chrono::seconds { 1 } ) return m_speed;

		m_speed = static_cast< std::uint64_t >(
			static_cast< long double >( total - m_previous )
		    / std::chrono::duration< long double >( elapsed ).count() );
		m_previous = total;
		m_at = now;
		return m_speed;
	}
};
} // namespace idhan::downloader
