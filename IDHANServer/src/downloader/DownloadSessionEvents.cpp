#include "DownloadSessionEvents.hpp"

#include <json/json.h>

#include "DownloadSessionEventHub.hpp"
#include "logging/log.hpp"

namespace idhan::downloader
{

void DownloadSessionEvents::handleNewConnection(
	[[maybe_unused]] const drogon::HttpRequestPtr& request,
	const drogon::WebSocketConnectionPtr& connection )
{
	log::debug( "Download session events: client connected" );
	DownloadSessionEventHub::instance().subscribe( connection, {}, true );
}

void DownloadSessionEvents::handleNewMessage(
	const drogon::WebSocketConnectionPtr& connection,
	std::string&& message,
	const drogon::WebSocketMessageType& type )
{
	if ( type != drogon::WebSocketMessageType::Text ) return;

	Json::Value body {};
	Json::CharReaderBuilder builder {};
	std::string errors {};
	const std::unique_ptr< Json::CharReader > reader { builder.newCharReader() };

	if ( !reader->parse( message.data(), message.data() + message.size(), &body, &errors ) || !body.isObject() )
	{
		log::debug( "Download session events: ignoring unparseable message" );
		return;
	}

	if ( body[ "action" ].asString() != "subscribe" ) return;

	const bool all { !body[ "session_ids" ].isArray() };
	std::vector< DownloadSessionID > sessions {};

	for ( const auto& entry : body[ "session_ids" ] )
		if ( entry.isIntegral() ) sessions.emplace_back( entry.as< DownloadSessionID >() );

	DownloadSessionEventHub::instance().subscribe( connection, std::move( sessions ), all );
}

void DownloadSessionEvents::handleConnectionClosed( const drogon::WebSocketConnectionPtr& connection )
{
	log::debug( "Download session events: client disconnected" );
	DownloadSessionEventHub::instance().unsubscribe( connection );
}

} // namespace idhan::downloader
