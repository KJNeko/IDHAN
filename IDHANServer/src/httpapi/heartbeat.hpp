#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "idhan/http/RouteSpec.hpp"
#include "idhan/http/SSEEventHub.hpp"

namespace idhan::httpapi
{

class HeartbeatService
{
	std::mutex m_timer_mutex;
	std::condition_variable_any m_timer_wakeup;
	http::SSEEventHub m_events;
	std::jthread m_timer;

	void run( std::stop_token );

  public:

	HeartbeatService();
	HeartbeatService( const HeartbeatService& ) = delete;
	HeartbeatService& operator=( const HeartbeatService& ) = delete;
	~HeartbeatService();

	[[nodiscard]] http::RouteDefinition route();
	void shutdown();
};

} // namespace idhan::httpapi
