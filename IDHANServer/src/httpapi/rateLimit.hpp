#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct RateLimitResponse
{
	std::string lane;
	std::string group;
	std::uint64_t requests;
	std::uint64_t seconds;
	std::int64_t remaining_ms;
	bool backed_off;
	std::size_t in_flight;
	std::size_t concurrency_limit;
	std::uint64_t dispatched;
	std::string dispatch_wait;
	std::size_t queued;
	std::uint64_t download_bytes_per_second;
	bool active;
	std::uint64_t rate_limit_hits;
	bool rate_limited;
	std::string advertised_limits;
	bool spread;
	std::uint64_t parser_error_hits;
	bool parser_limited;
	bool disabled;
};

struct ResetRateLimitResponse
{
	std::string reset;
};

struct ResetQuery
{
	http::possiblyMissing< std::string > lane;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/rate_limits"}]]
[[= http::Group{"Network"}]]
[[= http::Group{"Rate Limits"}]]
http::HTTPTask< http::JsonResult< std::vector< RateLimitResponse > > > list( [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/rate_limits/reset"}]]
[[= http::Group{"Network"}]]
[[= http::Group{"Rate Limits"}]]
http::HTTPTask< http::JsonResult< ResetRateLimitResponse > > reset( http::Query< ResetQuery > query );
// clang-format on

struct BandwidthResponse
{
	std::string host;
	std::int64_t bytes_received {};
	std::vector< BandwidthResponse > children {};
};

struct BandwidthRequest
{
	// Unix seconds, inclusive start and exclusive end; absent bounds include all history.
	http::possiblyMissing< std::int64_t > start;
	http::possiblyMissing< std::int64_t > end;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/bandwidth"}]]
[[= http::Group{"Network"}]]
http::HTTPTask< http::JsonResult< std::vector< BandwidthResponse > > > listBandwidth( db::DBContext& db, http::Query< BandwidthRequest > query );
// clang-format on

} // namespace idhan::httpapi
