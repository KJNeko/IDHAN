#include "RateLimitEvents.hpp"

#include "RateLimitEventHub.hpp"
#include "logging/log.hpp"

namespace idhan::downloader
{

void RateLimitEvents::handleNewConnection(
	[[maybe_unused]] const drogon::HttpRequestPtr& request,
	const drogon::WebSocketConnectionPtr& connection )
{
	log::debug( "Rate limit events: client connected" );
	RateLimitEventHub::instance().subscribe( connection );
}

void RateLimitEvents::handleNewMessage(
	[[maybe_unused]] const drogon::WebSocketConnectionPtr& connection,
	[[maybe_unused]] std::string&& message,
	[[maybe_unused]] const drogon::WebSocketMessageType& type )
{}

void RateLimitEvents::handleConnectionClosed( const drogon::WebSocketConnectionPtr& connection )
{
	log::debug( "Rate limit events: client disconnected" );
	RateLimitEventHub::instance().unsubscribe( connection );
}

} // namespace idhan::downloader
