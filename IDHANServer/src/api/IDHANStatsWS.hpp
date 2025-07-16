//
// Created by kj16609 on 6/14/25.
//
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include <drogon/HttpController.h>
#include <drogon/WebSocketController.h>
#pragma GCC diagnostic pop



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
