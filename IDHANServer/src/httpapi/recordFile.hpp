#pragma once

#include <string>

#include "IDHANTypes.hpp"
#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition recordFileRoute( db::DBContext& db, std::string path );

[[nodiscard]] http::HTTPTask< http::Response > recordFileResponse(
	db::DBContext& db,
	http::RequestContext& context,
	RecordID record_id );

} // namespace idhan::httpapi
