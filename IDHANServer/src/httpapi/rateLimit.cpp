#include "rateLimit.hpp"

#include <chrono>
#include <limits>

#include "db/bandwidth.hpp"
#include "downloader/BandwidthTree.hpp"

#include "apiAuth.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static std::int64_t milliseconds( const std::chrono::steady_clock::duration value )
{
	if ( value <= std::chrono::steady_clock::duration::zero() ) return 0;

	return std::chrono::ceil< std::chrono::milliseconds >( value ).count();
}

http::HTTPTask< http::JsonResult< std::vector< RateLimitResponse > > > list(
	[[maybe_unused]] http::RequestContext context )
{
	std::vector< RateLimitResponse > response;
	for ( const auto& lane : downloader::downloadSessionManager().laneSnapshots() )
	{
		response.push_back(
			{ lane.key,
			  lane.group,
			  lane.rate.requests,
			  lane.rate.seconds,
			  milliseconds( lane.remaining ),
			  lane.backed_off,
			  lane.in_flight,
			  lane.concurrency_limit,
			  lane.dispatched,
			  lane.dispatch_wait,
			  lane.queued,
			  lane.download_bytes_per_second,
			  lane.active,
			  lane.rate_limit_hits,
			  lane.rate_limited,
			  lane.advertised_limits,
			  lane.rate.spread,
			  lane.parser_error_hits,
			  lane.parser_limited,
			  lane.disabled } );
	}
	co_return http::JsonResponse< std::vector< RateLimitResponse > > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< ResetRateLimitResponse > > reset( http::Query< ResetQuery > query )
{
	const auto lane { query.value.lane.value_or( "" ) };
	const auto result { co_await downloader::downloadSessionManager().resetBackoff( lane ) };

	if ( !result ) co_return http::Response::error( 400, result.error() );

	co_return http::JsonResponse< ResetRateLimitResponse > { { lane } };
}

http::HTTPTask< http::JsonResult< std::vector< BandwidthResponse > > > listBandwidth(
	db::DBContext& db,
	http::Query< BandwidthRequest > query )
{
	const auto start { query.value.start.value_or( std::numeric_limits< std::int64_t >::min() ) };
	const auto end { query.value.end.value_or( std::numeric_limits< std::int64_t >::max() ) };
	if ( start >= end ) co_return http::Response::error( 400, "start must be before end (Unix seconds)" );

	const auto totals { co_await db::bandwidthTotals( db, start, end ) };
	co_return http::JsonResponse< std::vector< BandwidthResponse > > {
		downloader::bandwidthTree< BandwidthResponse >( totals )
	};
}

} // namespace idhan::httpapi
