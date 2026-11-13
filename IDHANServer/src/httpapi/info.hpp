#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
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

struct FetchLogQuery
{
	http::possiblyMissing< std::string > level;
	http::possiblyMissing< std::uint64_t > since;
	http::possiblyMissing< std::string > format;
	http::possiblyMissing< std::uint64_t > after;
	http::possiblyMissing< std::uint64_t > limit;
};

using FetchLogEntriesQuery = FetchLogQuery;

struct LogEntryResponse
{
	std::uint64_t t;
	std::string level;
	std::uint64_t thread;
	std::string message;
	http::possiblyMissing< std::string > marker {};
};

struct LogEntriesResponse
{
	std::vector< LogEntryResponse > entries;
	bool truncated;
	std::optional< std::uint64_t > ready_at;
};

using LogResponse = std::variant<
	http::JsonResponse< LogEntriesResponse >,
	http::EncodedResponse< std::string, "text/plain" >,
	http::Response >;

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/api"}]]
[[= http::Public {}]]
[[= http::UnrestrictedParameters {}]]
http::HTTPTask< http::EncodedResult< std::string, "text/html" > > apiDocsIndex( http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/log/pretty"}]]
[[= http::Public {}]]
[[= http::UnrestrictedParameters {}]]
http::HTTPTask< http::EncodedResult< std::string, "text/html" > > logPretty( http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/log"}]]
[[= http::Group{"Logs"}]]
http::HTTPTask< LogResponse > fetchLog( http::Query< FetchLogQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/log/pretty/entries"}]]
[[= http::Group{"Logs"}]]
http::HTTPTask< LogResponse > fetchLogEntries( http::Query< FetchLogEntriesQuery > query );
// clang-format on

} // namespace idhan::httpapi
