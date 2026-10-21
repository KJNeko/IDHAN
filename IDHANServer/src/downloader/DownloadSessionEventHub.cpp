#include "DownloadSessionEventHub.hpp"

#include "logging/log.hpp"

namespace idhan::downloader
{

DownloadSessionEventHub& DownloadSessionEventHub::instance()
{
	static DownloadSessionEventHub hub {};
	return hub;
}

http::SSESubscription DownloadSessionEventHub::subscribe(
	std::vector< DownloadSessionID > sessions,
	const std::vector< DownloadSessionID >& existing )
{
	std::vector< std::string > entities {};
	entities.reserve( sessions.size() );
	for ( const auto session_id : sessions ) entities.emplace_back( std::to_string( session_id ) );

	std::string ids {};
	for ( const auto session_id : existing )
	{
		if ( !ids.empty() ) ids += ',';
		ids += std::to_string( session_id );
	}

	std::vector< http::ValidatedJson > snapshots {};
	if ( auto json { http::ValidatedJson::parse( std::format( "{{\"sessions\":[{}]}}", ids ) ) } )
		snapshots.emplace_back( std::move( *json ) );

	return m_events.subscribe( std::move( entities ), std::move( snapshots ), false );
}

void DownloadSessionEventHub::notify( const DownloadSessionID session_id )
{
	auto json { http::ValidatedJson::parse( std::format( "{{\"id\":{}}}", session_id ) ) };
	if ( !json ) return;
	m_events.publish( std::to_string( session_id ), std::move( *json ) );
}

void DownloadSessionEventHub::notifyRemoved( const DownloadSessionID session_id )
{
	auto json { http::ValidatedJson::parse( std::format( "{{\"id\":{},\"removed\":true}}", session_id ) ) };
	if ( !json ) return;
	m_events.remove( std::to_string( session_id ) );
	m_events.publish( std::to_string( session_id ), std::move( *json ) );
}

void DownloadSessionEventHub::shutdown()
{
	m_events.shutdown();
}

} // namespace idhan::downloader
