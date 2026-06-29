//
// Created by kj16609 on 6/14/25.
//

#include "Heartbeat.hpp"

#include "logging/log.hpp"

namespace idhan::api
{

struct HeartbeatContext
{
	trantor::TimerId timer_id { trantor::InvalidTimerId };
};

void sendStatusJson( const drogon::WebSocketConnectionPtr& wsConnPtr )
{
	Json::Value json {};
	json[ "status" ] = "ok";

	wsConnPtr->sendJson( json );
}

void Heartbeat::handleNewConnection(
	[[maybe_unused]] const drogon::HttpRequestPtr& req,
	const drogon::WebSocketConnectionPtr& wsConnPtr )
{
	log::info( "WS open" );

	auto ctx = std::make_shared< HeartbeatContext >();
	wsConnPtr->setContext( ctx );

	auto task = [ wsConnPtr ]()
	{
		if ( wsConnPtr->connected() )
			sendStatusJson( wsConnPtr );
	};

	ctx->timer_id = drogon::app().getLoop()->runEvery( 10.0, task );
	sendStatusJson( wsConnPtr );
}

void Heartbeat::handleNewMessage(
	const drogon::WebSocketConnectionPtr&,
	std::string&&,
	const drogon::WebSocketMessageType& )
{}

void Heartbeat::handleConnectionClosed( const drogon::WebSocketConnectionPtr& wsConnPtr )
{
	log::info( "WS closed" );
	auto ctx = wsConnPtr->getContext< HeartbeatContext >();
	if ( ctx && ctx->timer_id != trantor::InvalidTimerId )
		drogon::app().getLoop()->invalidateTimer( ctx->timer_id );
}

} // namespace idhan::api
