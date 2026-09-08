#pragma once

#include <IDHANDownloader/LaneObserver.hpp>
#include <drogon/WebSocketConnection.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IDHANTypes.hpp"

namespace idhan::downloader
{

class RateLimitEventHub final : public LaneObserver
{
	struct StoredSnapshot
	{
		LaneSnapshot snapshot {};
		std::chrono::steady_clock::time_point captured_at {};
	};

	std::mutex m_mutex {};
	std::unordered_map< std::string, StoredSnapshot > m_limits {};
	std::unordered_map< const drogon::WebSocketConnection*, std::weak_ptr< drogon::WebSocketConnection > >
		m_subscribers {};

	void broadcast( const LaneSnapshot& snapshot );

  public:

	static RateLimitEventHub& instance();
	void subscribe( const drogon::WebSocketConnectionPtr& connection );
	void unsubscribe( const drogon::WebSocketConnectionPtr& connection );

	void onLaneChanged( const LaneSnapshot& snapshot ) override;
};

} // namespace idhan::downloader
