#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <stop_token>
#include <vector>

#include "IDHANTypes.hpp"

namespace idhan::subscriptions
{
class SubscriptionUpdateQueue
{
	std::mutex m_mutex {};
	std::condition_variable_any m_readable {};
	std::set< DownloadSessionID > m_pending {};
	std::chrono::milliseconds m_interval;

  public:

	explicit SubscriptionUpdateQueue( std::chrono::milliseconds interval = std::chrono::milliseconds { 100 } ) :
	  m_interval( interval )
	{}

	void enqueue( const DownloadSessionID session_id )
	{
		{
			const std::lock_guard lock { m_mutex };
			m_pending.insert( session_id );
		}
		m_readable.notify_one();
	}

	// The deadline belongs to the first event; ongoing activity never postpones a flush.
	[[nodiscard]] std::vector< DownloadSessionID > waitForBatch( const std::stop_token stop )
	{
		std::unique_lock lock { m_mutex };
		m_readable.wait( lock, stop, [ this ] { return !m_pending.empty(); } );
		if ( stop.stop_requested() ) return {};

		m_readable.wait_for( lock, stop, m_interval, [] { return false; } );
		if ( stop.stop_requested() ) return {};

		std::vector< DownloadSessionID > result { m_pending.begin(), m_pending.end() };
		m_pending.clear();
		return result;
	}
};
} // namespace idhan::subscriptions
