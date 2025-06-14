//
// Created by kj16609 on 6/14/25.
//
#pragma once

#include <drogon/HttpController.h>

#include "drogon/WebSocketController.h"

namespace idhan::api
{

class IDHANStatsWS : public drogon::WebSocketController< IDHANStatsWS >
{
	// drogon::Task< drogon::HttpResponsePtr > getBones( drogon::HttpRequestPtr request );
	// drogon::Task< drogon::HttpResponsePtr > getFileCount( drogon::HttpRequestPtr request );

	void handleNewConnection( const drogon::HttpRequestPtr&, const drogon::WebSocketConnectionPtr& ) override;
	void handleNewMessage( const drogon::WebSocketConnectionPtr&, std::string&&, const drogon::WebSocketMessageType& )
		override;
	void handleConnectionClosed( const drogon::WebSocketConnectionPtr& ) override;

  public:

	WS_PATH_LIST_BEGIN

	WS_PATH_ADD( "/heartbeat" );

	WS_PATH_LIST_END

	// METHOD_LIST_BEGIN
	// ADD_METHOD_TO( IDHANStatsAPI::getBones, "/bones", drogon::Get );
	// METHOD_LIST_END
};

} // namespace idhan::api
