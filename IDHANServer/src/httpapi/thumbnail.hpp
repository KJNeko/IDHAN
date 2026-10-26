#pragma once

#include "IDHANTypes.hpp"
#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition thumbnailRoute( db::DBContext& db );

[[nodiscard]] http::HTTPTask< http::Response > recordThumbnailResponse(
	db::DBContext& db,
	http::RequestContext& context,
	RecordID record_id );

} // namespace idhan::httpapi
