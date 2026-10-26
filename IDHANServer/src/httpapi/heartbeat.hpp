#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

namespace idhan::httpapi
{

class HeartbeatService
{
	std::mutex m_timer_mutex;
	std::condition_variable_any m_timer_wakeup;
	std::jthread m_timer;

	void run( std::stop_token );

  public:

	HeartbeatService();
	HeartbeatService( const HeartbeatService& ) = delete;
	HeartbeatService& operator=( const HeartbeatService& ) = delete;
	~HeartbeatService();

	void shutdown();
};

} // namespace idhan::httpapi
