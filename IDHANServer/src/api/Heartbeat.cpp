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

	// weak capture: the repeating timer outlives the connection (invalidation only takes
	// effect at the timer's next expiry), a strong capture would hold the closed
	// connection alive until then
	auto task = [ weak_conn = std::weak_ptr( wsConnPtr ) ]()
	{
		if ( const auto conn = weak_conn.lock(); conn && conn->connected() ) sendStatusJson( conn );
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
	if ( ctx && ctx->timer_id != trantor::InvalidTimerId ) drogon::app().getLoop()->invalidateTimer( ctx->timer_id );
}

} // namespace idhan::api
