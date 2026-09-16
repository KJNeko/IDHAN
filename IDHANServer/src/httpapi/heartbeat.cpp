#include "heartbeat.hpp"

#include <chrono>
#include <utility>

#include "idhan/http/SSEBodySource.hpp"

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
  m_events {},
  m_timer { &HeartbeatService::run, this }
{}

HeartbeatService::~HeartbeatService()
{
	shutdown();
	m_timer.request_stop();
	m_timer_wakeup.notify_all();
}

http::RouteDefinition HeartbeatService::route()
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/heartbeat", {}, {} },
		[ this ]( [[maybe_unused]] http::RequestContext context ) -> http::HTTPTask< http::Response >
		{
			http::Response response {};
			response.body = proxygen::coro::HTTPSourceHolder { new http::SSEBodySource {
				std::move( response.message ), m_events.subscribe( {} ) } };
			co_return response;
		}
	};
}

void HeartbeatService::shutdown()
{
	m_events.shutdown();
}

} // namespace idhan::httpapi
