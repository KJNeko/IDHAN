#include "DownloadSessionEventHub.hpp"
#include "DebugEvents.hpp"
#include "events/ServerEvents.hpp"

namespace idhan::downloader
{

DownloadSessionEventHub& DownloadSessionEventHub::instance()
{
	static DownloadSessionEventHub hub {};
	return hub;
}

void DownloadSessionEventHub::notify( const DownloadSessionID session_id )
{
	notifyDebugChanged();
	auto json { http::ValidatedJson::parse( std::format( "{{\"id\":{}}}", session_id ) ) };
	if ( !json ) return;
	server::events().publish( "downloader.session", std::to_string( session_id ), std::move( *json ) );
}

void DownloadSessionEventHub::notifyRemoved( const DownloadSessionID session_id )
{
	notifyDebugChanged();
	auto json { http::ValidatedJson::parse( std::format( "{{\"id\":{},\"removed\":true}}", session_id ) ) };
	if ( !json ) return;
	server::events().remove( "downloader.session", std::to_string( session_id ), std::move( *json ) );
}

} // namespace idhan::downloader
