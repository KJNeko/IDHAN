#pragma once

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

} // namespace idhan::downloader
