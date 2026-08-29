#include "DownloadSessionEvents.hpp"

#include <json/json.h>

#include "logging/log.hpp"
#include "sessionTree.hpp"

namespace idhan::downloader
{

constexpr double coalesce_delay_seconds { 0.2 };

DownloadSessionEventHub& DownloadSessionEventHub::instance()
{
	static DownloadSessionEventHub hub {};
	return hub;
}

void DownloadSessionEventHub::subscribe(
	const drogon::WebSocketConnectionPtr& connection,
	std::vector< DownloadSessionID > sessions,
	const bool all )
{
	{
		std::lock_guard lock { m_mutex };
		auto& subscription { m_subscribers[ connection.get() ] };
		subscription.connection = connection;
		subscription.all = all;
		subscription.sessions.clear();
		for ( const auto session_id : sessions ) subscription.sessions.emplace( session_id );
	}

	if ( all )
		sendAllSessions( connection );
	else
		for ( const auto session_id : sessions ) sendSession( connection, session_id );
}

void DownloadSessionEventHub::unsubscribe( const drogon::WebSocketConnectionPtr& connection )
{
	std::lock_guard lock { m_mutex };
	m_subscribers.erase( connection.get() );
}

void DownloadSessionEventHub::notify( const DownloadSessionID session_id )
{
	{
		std::lock_guard lock { m_mutex };
		if ( m_subscribers.empty() ) return;
		m_dirty.emplace( session_id );
		if ( m_flush_scheduled ) return;
		m_flush_scheduled = true;
	}

	drogon::app().getLoop()->runAfter( coalesce_delay_seconds, [ this ] { flush(); } );
}

void DownloadSessionEventHub::flush()
{
	std::unordered_set< DownloadSessionID > dirty {};
	{
		std::lock_guard lock { m_mutex };
		m_flush_scheduled = false;
		dirty.swap( m_dirty );
	}

	for ( const auto session_id : dirty )
	{
		std::vector< drogon::WebSocketConnectionPtr > targets {};
		{
			std::lock_guard lock { m_mutex };
			for ( auto& [ key, subscription ] : m_subscribers )
			{
				(void)key;
				if ( !subscription.all && !subscription.sessions.contains( session_id ) ) continue;
				if ( const auto connection { subscription.connection.lock() }; connection && connection->connected() )
					targets.emplace_back( connection );
			}
		}

		if ( targets.empty() ) continue;

		drogon::async_run(
			[ session_id, targets = std::move( targets ) ]() -> drogon::Task< void >
			{
				const auto db { drogon::app().getDbClient() };
				const auto session { co_await sessionSummary( db, session_id ) };

				Json::Value message {};

				if ( session.isNull() )
				{
					message[ "type" ] = "session_removed";
					message[ "session_id" ] = session_id;
				}
				else
				{
					message[ "type" ] = "session";
					message[ "session" ] = session;
					message[ "urls" ] = co_await sessionUrlTree( db, session_id, false );
				}

				for ( const auto& connection : targets )
					if ( connection->connected() ) connection->sendJson( message );
			} );
	}
}

void DownloadSessionEventHub::notifyRemoved( const DownloadSessionID session_id )
{
	notify( session_id );
}

void DownloadSessionEventHub::sendSession(
	const drogon::WebSocketConnectionPtr& connection,
	const DownloadSessionID session_id )
{
	drogon::async_run(
		[ connection, session_id ]() -> drogon::Task< void >
		{
			const auto db { drogon::app().getDbClient() };
			const auto session { co_await sessionSummary( db, session_id ) };
			if ( session.isNull() || !connection->connected() ) co_return;

			Json::Value message {};
			message[ "type" ] = "session";
			message[ "session" ] = session;
			message[ "urls" ] = co_await sessionUrlTree( db, session_id, false );
			if ( connection->connected() ) connection->sendJson( message );
		} );
}

void DownloadSessionEventHub::sendAllSessions( const drogon::WebSocketConnectionPtr& connection )
{
	drogon::async_run(
		[ connection ]() -> drogon::Task< void >
		{
			const auto db { drogon::app().getDbClient() };
			const auto rows { co_await db->execSqlCoro(
				"SELECT download_session_id FROM download_sessions "
				"ORDER BY last_used_at DESC, download_session_id DESC" ) };

			Json::Value ids { Json::arrayValue };
			for ( const auto& row : rows ) ids.append( row[ "download_session_id" ].as< DownloadSessionID >() );

			Json::Value opening {};
			opening[ "type" ] = "sessions";
			opening[ "session_ids" ] = ids;
			if ( !connection->connected() ) co_return;
			connection->sendJson( opening );

			for ( const auto& row : rows )
			{
				if ( !connection->connected() ) co_return;
				const auto session_id { row[ "download_session_id" ].as< DownloadSessionID >() };
				const auto session { co_await sessionSummary( db, session_id ) };
				if ( session.isNull() ) continue;

				Json::Value message {};
				message[ "type" ] = "session";
				message[ "session" ] = session;
				message[ "urls" ] = co_await sessionUrlTree( db, session_id, false );
				if ( connection->connected() ) connection->sendJson( message );
			}
		} );
}

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
