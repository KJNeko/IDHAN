#pragma once

#include <string>

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

struct FetchRecordFileQuery;

[[nodiscard]] http::HTTPTask< http::Response > recordFileResponse(
	db::DBContext& db,
	http::RequestContext& context,
	RecordID record_id,
	const FetchRecordFileQuery& parameters );

struct FetchRecordFileQuery
{
	http::possiblyMissing< bool > download;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}/file"}]]
[[= http::Group{"Files"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}"}]]
http::HTTPTask< http::EncodedResult< http::RequestBody, "*/*" > > fetchRecordFile( db::DBContext& db, http::RequestContext context, http::Query< FetchRecordFileQuery > query );
// clang-format on

} // namespace idhan::httpapi
