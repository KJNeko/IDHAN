#include "DebugEvents.hpp"

namespace idhan::downloader
{
http::SSEEventHub& debugEvents()
{
	static http::SSEEventHub events;
	return events;
}

void notifyDebugChanged()
{
	static const auto changed { http::ValidatedJson::parse( "{}" ).value() };
	debugEvents().publish( "debug", changed );
}
} // namespace idhan::downloader
