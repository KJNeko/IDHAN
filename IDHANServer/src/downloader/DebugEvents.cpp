#include "DebugEvents.hpp"
#include "events/ServerEvents.hpp"

namespace idhan::downloader
{
http::SSEEventHub& debugEvents()
{
	return server::events();
}

void notifyDebugChanged()
{
	static const auto changed { http::ValidatedJson::parse( "{}" ).value() };
	debugEvents().publish( "downloader.debug", "", changed, false );
}
} // namespace idhan::downloader
