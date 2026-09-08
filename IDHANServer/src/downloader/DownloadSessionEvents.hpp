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

#include "api/APIAuth.hpp"

namespace idhan::downloader
{

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
