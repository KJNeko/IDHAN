//
// Created by kj16609 on 3/26/26.
//

#include "logging/log.hpp"

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <chrono>
#include <memory>

#include "Config.hpp"
#include "api/InfoAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
{

namespace
{
auto parseLevelString( const std::string& level_str )
{
	if ( level_str == "trace" ) return spdlog::level::trace;
	if ( level_str == "debug" ) return spdlog::level::debug;
	if ( level_str == "info" ) return spdlog::level::info;
	if ( level_str == "warning" || level_str == "warn" ) return spdlog::level::warn;
	if ( level_str == "error" || level_str == "err" ) return spdlog::level::err;
	if ( level_str == "critical" ) return spdlog::level::critical;
	return spdlog::level::info;
}
} // namespace

drogon::Task< drogon::HttpResponsePtr > InfoAPI::log( drogon::HttpRequestPtr request )
{
	// /log?since=<unix_timestamp>&level=<level>

	auto logger { log::getServerLogger() };

	if ( !logger ) co_return createInternalError( "Could not get default spdlog logger" );

	logger->flush();

	const auto level_str { request->getOptionalParameter< std::string >( "level" ) };
	const auto req_level { parseLevelString( level_str.value_or( "info" ) ) };
	// unix timestamp in seconds, matching the granularity compared below
	const auto since_seconds { request->getOptionalParameter< std::uint64_t >( "since" ) };

	auto ring_sink { log::getServerRingBufferSink() };

	if ( !ring_sink ) co_return createInternalError( "Could not find Ring buffer sink in logger" );

	constexpr std::string_view server_fmt { "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v" };
	spdlog::pattern_formatter formatter { std::string( server_fmt ) };

	const auto raw_entries { ring_sink->last_raw( 0 ) };

	std::string log_out {};
	log_out.reserve( 1024 * 8 );

	for ( const auto& entry : raw_entries )
	{
		if ( entry.level < req_level ) continue;

		if ( since_seconds.has_value() )
		{
			const auto entry_seconds {
				std::chrono::duration_cast< std::chrono::seconds >( entry.time.time_since_epoch() ).count()
			};
			if ( static_cast< std::uint64_t >( entry_seconds ) < *since_seconds ) continue;
		}

		spdlog::memory_buf_t buf;
		formatter.format( entry, buf );
		log_out += SPDLOG_BUF_TO_STRING( buf );
	}

	auto response { drogon::HttpResponse::newHttpResponse() };
	response->setBody( log_out );
	response->setStatusCode( drogon::HttpStatusCode::k200OK );
	response->setContentTypeCode( drogon::CT_TEXT_PLAIN );

	co_return response;
}

} // namespace idhan::api