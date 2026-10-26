#include "rateLimit.hpp"

#include <folly/io/IOBuf.h>

#include <chrono>
#include <simdjson.h>

#include "apiAuth.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "downloader/RateLimitEventHub.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/SSEBodySource.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::int64_t milliseconds( const std::chrono::steady_clock::duration value )
{
	if ( value <= std::chrono::steady_clock::duration::zero() ) return 0;

	return std::chrono::ceil< std::chrono::milliseconds >( value ).count();
}

static http::HTTPTask< http::Response > list( [[maybe_unused]] http::RequestContext context )
{
	simdjson::builder::string_builder builder;
	builder.start_array();

	bool first { true };
	for ( const auto& lane : downloader::downloadSessionManager().laneSnapshots() )
	{
		if ( !first ) builder.append_comma();
		first = false;

		builder.start_object();
		builder.append_key_value( "lane", lane.key );
		builder.append_comma();
		builder.append_key_value( "group", lane.group.value_or( "" ) );
		builder.append_comma();
		builder.append_key_value( "throttled", lane.throttled );
		builder.append_comma();
		builder.append_key_value( "requests", lane.rate_requests );
		builder.append_comma();
		builder.append_key_value( "seconds", lane.rate_seconds );
		builder.append_comma();
		builder.append_key_value( "effective_interval_ms", milliseconds( lane.effective_interval ) );
		builder.append_comma();
		builder.append_key_value( "remaining_ms", milliseconds( lane.remaining ) );
		builder.append_comma();
		builder.append_key_value( "consecutive_failures", lane.consecutive_failures );
		builder.append_comma();
		builder.append_key_value( "backed_off", lane.backed_off );
		builder.append_comma();
		builder.append_key_value( "in_flight", static_cast< std::uint64_t >( lane.in_flight ) );
		builder.append_comma();
		builder.append_key_value( "queued", static_cast< std::uint64_t >( lane.queued ) );
		builder.append_comma();
		builder.append_key_value( "shards", static_cast< std::uint64_t >( lane.shards ) );
		builder.append_comma();
		builder.append_key_value( "download_bytes_per_second", lane.download_bytes_per_second );
		builder.append_comma();
		builder.append_key_value( "active", lane.active );
		builder.end_object();
	}

	builder.end_array();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > reset( http::RequestContext context )
{
	const auto lane_e { context.parameters().optional< std::string >( "lane" ) };
	if ( !lane_e ) co_return http::Response::error( 400, lane_e.error().message );

	const auto lane { lane_e->value_or( "" ) };
	const auto result { downloader::downloadSessionManager().resetBackoff( lane ) };

	if ( !result ) co_return http::Response::error( 400, result.error() );

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "reset", lane );
	builder.end_object();

	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	co_return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

http::RouteDefinition rateLimitListRoute( db::DBContext& db )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/rate_limits", {}, { apiAuthValidation( db ) } }, list
	};
}

http::RouteDefinition rateLimitResetRoute( db::DBContext& db )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::POST, "/rate_limits/reset", {}, { apiAuthValidation( db ) } }, reset
	};
}

http::RouteDefinition rateLimitEventsRoute( db::DBContext& db )
{
	return http::RouteDefinition {
		http::RouteMetadata { proxygen::HTTPMethod::GET, "/rate_limits/events", {}, { apiAuthValidation( db ) } },
		[]( [[maybe_unused]] http::RequestContext context ) -> http::HTTPTask< http::Response >
		{
			http::Response response {};
			response.body = proxygen::coro::HTTPSourceHolder { new http::SSEBodySource {
				std::move( response.message ), downloader::RateLimitEventHub::instance().subscribe() } };
			co_return response;
		}
	};
}

} // namespace idhan::httpapi
