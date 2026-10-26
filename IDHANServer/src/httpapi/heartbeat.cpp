#include "heartbeat.hpp"

#include <chrono>
#include <utility>

#include "idhan/http/SSEEventHub.hpp"

namespace idhan::httpapi
{

void HeartbeatService::run( const std::stop_token stop_token )
{
	std::unique_lock lock { m_timer_mutex };
	while ( !stop_token.stop_requested() )
	{
		m_timer_wakeup.wait_for(
			lock, stop_token, std::chrono::seconds { 10 }, [ &stop_token ] { return stop_token.stop_requested(); } );

		if ( stop_token.stop_requested() ) break;

		http::SSEEventHub::heartbeatAll();
	}
}

HeartbeatService::HeartbeatService() :
  m_timer_mutex {},
  m_timer_wakeup {},
  m_timer { &HeartbeatService::run, this }
{}

HeartbeatService::~HeartbeatService()
{
	shutdown();
}

void HeartbeatService::shutdown()
{
	m_timer.request_stop();
	m_timer_wakeup.notify_all();
	if ( m_timer.joinable() ) m_timer.join();
}

} // namespace idhan::httpapi
