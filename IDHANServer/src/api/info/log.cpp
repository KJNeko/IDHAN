//
// Created by kj16609 on 3/26/26.
//

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <chrono>
#include <memory>

#include "Config.hpp"
#include "api/InfoAPI.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::api
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

drogon::Task< drogon::HttpResponsePtr > InfoAPI::log( drogon::HttpRequestPtr request )
{
	// /log?since=<unix_timestamp>&level=<level>

	auto logger { spdlog::get( "default" ) };
	logger->flush();

	const auto level_str { request->getOptionalParameter< std::string >( "level" ) };
	const auto req_level { parseLevelString( level_str.value_or( "info" ) ) };
	const auto since_ms { request->getOptionalParameter< std::uint64_t >( "since" ) };

	// Locate the ringbuffer sink in the logger
	std::shared_ptr< spdlog::sinks::ringbuffer_sink_mt > ring_sink;
	for ( auto& s : logger->sinks() )
	{
		ring_sink = std::dynamic_pointer_cast< spdlog::sinks::ringbuffer_sink_mt >( s );
		if ( ring_sink ) break;
	}

	if ( !ring_sink )
	{
		auto response { drogon::HttpResponse::newHttpResponse() };
		response->setBody( "Ring buffer sink not available\n" );
		response->setStatusCode( drogon::HttpStatusCode::k500InternalServerError );
		response->setContentTypeCode( drogon::CT_TEXT_PLAIN );
		co_return response;
	}

	constexpr std::string_view server_fmt { "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v" };
	spdlog::pattern_formatter formatter { std::string( server_fmt ) };

	const auto raw_entries { ring_sink->last_raw( 0 ) };

	std::string log_out {};
	log_out.reserve( 1024 * 8 );

	for ( const auto& entry : raw_entries )
	{
		if ( entry.level < req_level ) continue;

		if ( since_ms.has_value() )
		{
			const auto entry_ms {
				std::chrono::duration_cast< std::chrono::seconds >( entry.time.time_since_epoch() ).count()
			};
			if ( static_cast< std::uint64_t >( entry_ms ) < *since_ms ) continue;
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