#include "RateLimitAPI.hpp"

#include <json/json.h>

#include <chrono>

#include "api/helpers/ResponseCallback.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "downloader/DownloadSessionManager.hpp"

namespace idhan::api
{

static Json::UInt64 milliseconds( const std::chrono::steady_clock::duration value )
{
	if ( value <= std::chrono::steady_clock::duration::zero() ) return 0;

	return static_cast< Json::UInt64 >( std::chrono::ceil< std::chrono::milliseconds >( value ).count() );
}

drogon::Task< drogon::HttpResponsePtr > RateLimitAPI::list( drogon::HttpRequestPtr )
{
	Json::Value json { Json::arrayValue };

	for ( const auto& lane : downloader::downloadSessionManager().laneSnapshots() )
	{
		Json::Value entry {};
		entry[ "lane" ] = lane.key;
		entry[ "group" ] = lane.group.value_or( "" );
		entry[ "throttled" ] = lane.throttled;
		entry[ "requests" ] = Json::UInt64 { lane.rate_requests };
		entry[ "seconds" ] = Json::UInt64 { lane.rate_seconds };
		entry[ "effective_interval_ms" ] = milliseconds( lane.effective_interval );
		entry[ "remaining_ms" ] = milliseconds( lane.remaining );
		entry[ "consecutive_failures" ] = lane.consecutive_failures;
		entry[ "backed_off" ] = lane.backed_off;
		entry[ "in_flight" ] = Json::UInt64 { lane.in_flight };
		entry[ "queued" ] = Json::UInt64 { lane.queued };
		entry[ "shards" ] = Json::UInt64 { lane.shards };
		entry[ "active" ] = lane.active;
		json.append( std::move( entry ) );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( std::move( json ) );
}

drogon::Task< drogon::HttpResponsePtr > RateLimitAPI::reset( drogon::HttpRequestPtr request )
{
	const auto lane { request->getOptionalParameter< std::string >( "lane" ) };
	const auto result { downloader::downloadSessionManager().resetBackoff( lane.value_or( "" ) ) };

	if ( !result ) co_return createBadRequest( "{}", result.error() );

	Json::Value json {};
	json[ "reset" ] = lane.value_or( "" );
	co_return drogon::HttpResponse::newHttpJsonResponse( std::move( json ) );
}

} // namespace idhan::api
