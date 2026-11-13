#include "heartbeat.hpp"

#include <chrono>
#include "downloader/DownloadSessionEventHub.hpp"
#include "logging/log.hpp"
#include <utility>

#include "idhan/http/streaming/SSEEventHub.hpp"

namespace idhan::httpapi
{

void HeartbeatService::run( const std::stop_token stop_token )
{
	std::unique_lock lock { m_timer_mutex };
	auto next_heartbeat { std::chrono::steady_clock::now() };
	while ( !stop_token.stop_requested() )
	{
		m_timer_wakeup.wait_for(
			lock, stop_token, std::chrono::seconds { 1 }, [ &stop_token ] { return stop_token.stop_requested(); } );

		if ( stop_token.stop_requested() ) break;

		try
		{
			downloader::DownloadSessionEventHub::instance().flush();
		}
		catch ( const std::exception& error )
		{
			log::error( "Session event flush failed: {}", error.what() );
		}
		const auto now { std::chrono::steady_clock::now() };
		if ( now >= next_heartbeat )
		{
			http::SSEEventHub::heartbeatAll();
			next_heartbeat = now + std::chrono::seconds { 10 };
		}
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
