#pragma once

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

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "IDHANTypes.hpp"
#include "api/APIAuth.hpp"

namespace idhan::downloader
{

class DownloadSessionEventHub
{
	struct Subscription
	{
		std::weak_ptr< drogon::WebSocketConnection > connection {};
		std::unordered_set< DownloadSessionID > sessions {};
		bool all { true };
	};

	std::mutex m_mutex {};
	std::unordered_map< const drogon::WebSocketConnection*, Subscription > m_subscribers {};
	std::unordered_set< DownloadSessionID > m_dirty {};
	bool m_flush_scheduled { false };

	void flush();

  public:

	static DownloadSessionEventHub& instance();

	void subscribe(
		const drogon::WebSocketConnectionPtr& connection,
		std::vector< DownloadSessionID > sessions,
		bool all );
	void unsubscribe( const drogon::WebSocketConnectionPtr& connection );

	void notify( DownloadSessionID session_id );
	void notifyRemoved( DownloadSessionID session_id );

	void sendSession( const drogon::WebSocketConnectionPtr& connection, DownloadSessionID session_id );
	void sendAllSessions( const drogon::WebSocketConnectionPtr& connection );
};

class DownloadSessionEvents final : public drogon::WebSocketController< DownloadSessionEvents >
{
	void handleNewConnection( const drogon::HttpRequestPtr&, const drogon::WebSocketConnectionPtr& ) override;
	void handleNewMessage( const drogon::WebSocketConnectionPtr&, std::string&&, const drogon::WebSocketMessageType& )
		override;
	void handleConnectionClosed( const drogon::WebSocketConnectionPtr& ) override;

  public:

	WS_PATH_LIST_BEGIN

	WS_PATH_ADD( "/download_sessions/events", ::idhan::api::IDHANAPIAuthName );

	WS_PATH_LIST_END
};

} // namespace idhan::downloader
