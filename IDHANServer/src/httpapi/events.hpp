#pragma once

#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct EventsQuery
{
	http::possiblyMissing< std::string > filter;
	http::possiblyMissing< std::string > session_ids;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/events"}]]
[[= http::Group{"Events"}]]
http::HTTPTask< http::EncodedResult< std::string, "text/event-stream" > > events( db::DBContext& db, http::Query< EventsQuery > query );
// clang-format on

} // namespace idhan::httpapi
