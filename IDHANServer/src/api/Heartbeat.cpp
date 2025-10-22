//
// Created by kj16609 on 6/14/25.
//

#include "Heartbeat.hpp"

#include "logging/log.hpp"

namespace idhan::api
{

void sendStatusJson( const drogon::WebSocketConnectionPtr& wsConnPtr )
{
	Json::Value json {};
	json[ "status" ] = "ok";

	wsConnPtr->sendJson( json );
}

void Heartbeat::handleNewConnection(
	[[maybe_unused]] const drogon::HttpRequestPtr& req, const drogon::WebSocketConnectionPtr& wsConnPtr )
{
	std::shared_ptr< trantor::TimerId > timer_id { std::make_shared< trantor::TimerId >( 0 ) };
	log::info( "WS open" );

	using namespace std::chrono_literals;

	auto task = [ wsConnPtr, timer_id ]()
	{
		if ( wsConnPtr->connected() )
		{
			sendStatusJson( wsConnPtr );
		}
		else
		{
			// Kill the timer if we are no longer connected
			log::info( "WS closed" );
			drogon::app().getLoop()->invalidateTimer( *timer_id.get() );
		}
	};

	const auto id { drogon::app().getLoop()->runEvery( 10.0, task ) };
	sendStatusJson( wsConnPtr );

	*timer_id.get() = id;
}

void Heartbeat::
	handleNewMessage( const drogon::WebSocketConnectionPtr&, std::string&&, const drogon::WebSocketMessageType& )
{}

void Heartbeat::handleConnectionClosed( const drogon::WebSocketConnectionPtr& )
{}

} // namespace idhan::api