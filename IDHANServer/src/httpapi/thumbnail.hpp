#pragma once

#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"
#include "recordValidator.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct FetchThumbnailQuery;

[[nodiscard]] http::HTTPTask< http::Response > recordThumbnailResponse(
	db::DBContext& db,
	http::RequestContext& context,
	RecordID record_id,
	const FetchThumbnailQuery& parameters );

struct FetchThumbnailQuery
{
	http::possiblyMissing< bool > regenerate;
	http::possiblyMissing< std::size_t > size;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}/thumbnail"}]]
[[= http::Group{"Files"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::EncodedResult< http::RequestBody, "image/webp" > > fetchThumbnail( db::DBContext& db, http::RequestContext context, http::Query< FetchThumbnailQuery > query );
// clang-format on

} // namespace idhan::httpapi
