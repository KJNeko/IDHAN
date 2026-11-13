#include "IDHANDownloader/http/RequestTiming.hpp"

#include "js/ScriptTiming.hpp"
#include "logging/format_ns.hpp"

namespace idhan::downloader
{

static std::string sentTimestamp( const std::chrono::system_clock::time_point value )
{
	const auto stamp { std::chrono::floor< std::chrono::milliseconds >( value ) };
	const auto day { std::chrono::floor< std::chrono::days >( stamp ) };
	const std::chrono::year_month_day date { day };
	const std::chrono::hh_mm_ss time { stamp - day };
	return format_ns::format(
		"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} UTC",
		static_cast< int >( date.year() ),
		static_cast< unsigned >( date.month() ),
		static_cast< unsigned >( date.day() ),
		time.hours().count(),
		time.minutes().count(),
		time.seconds().count(),
		time.subseconds().count() );
}

std::string describeRequestTiming( const RequestTiming& timing )
{
	const std::string attempt { timing.attempts > 1 ? format_ns::format( ", attempt {}", timing.attempts ) : "" };
	if ( !timing.queued.has_value() ) return "never queued";

	const auto queued { timing.queued.value() };
	const auto preparation { formatScriptDuration( queued - timing.made ) };
	if ( !timing.sent.has_value() ) return format_ns::format( "not sent, preparation {}{}", preparation, attempt );

	const auto sent { timing.sent.value() };
	const auto timestamp {
		timing.sent_wall.has_value() ?
			format_ns::format( "sent at {}", sentTimestamp( timing.sent_wall.value() ) ) :
			std::string { "sent" }
	};
	const auto waiting { formatScriptDuration( sent - queued ) };
	if ( !timing.received.has_value() )
		return format_ns::format(
			"{}, waiting for response, wait before send {}, preparation {}{}",
			timestamp,
			waiting,
			preparation,
			attempt );

	return format_ns::format(
		"{}, transfer {}, wait before send {}, preparation {}{}",
		timestamp,
		formatScriptDuration( timing.received.value() - sent ),
		waiting,
		preparation,
		attempt );
}

} // namespace idhan::downloader
