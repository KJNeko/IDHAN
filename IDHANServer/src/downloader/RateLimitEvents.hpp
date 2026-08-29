#pragma once

#include <IDHANDownloader/LaneSnapshot.hpp>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include <drogon/HttpController.h>
#include <drogon/WebSocketController.h>
#pragma GCC diagnostic pop

#include <chrono>
#include <mutex>
#include <unordered_map>

#include "api/APIAuth.hpp"

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

class RateLimitEvents final : public drogon::WebSocketController< RateLimitEvents >
{
	void handleNewConnection( const drogon::HttpRequestPtr&, const drogon::WebSocketConnectionPtr& ) override;
	void handleNewMessage( const drogon::WebSocketConnectionPtr&, std::string&&, const drogon::WebSocketMessageType& )
		override;
	void handleConnectionClosed( const drogon::WebSocketConnectionPtr& ) override;

  public:

	WS_PATH_LIST_BEGIN

	WS_PATH_ADD( "/rate_limits/events", ::idhan::api::IDHANAPIAuthName );

	WS_PATH_LIST_END
};

} // namespace idhan::downloader
