#pragma once

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

struct JobStatusResponse
{
	JobID job_id;
	std::string job_name;
	bool continuous;
	std::string location;
	http::possiblyMissing< std::int64_t > start_time {};
	bool completed;
	std::string status;
	http::possiblyMissing< std::int64_t > end_time {};
	http::possiblyMissing< std::string > error {};
	// Each job defines its own progress and final response payloads.
	http::possiblyMissing< http::JsonDocument > progress {};
	http::possiblyMissing< http::JsonDocument > response {};
};

struct JobsStatusResponse
{
	std::vector< JobStatusResponse > jobs;
};

struct QueuedJobResponse
{
	JobID job_id;
};

struct JobsStatusQuery
{
	http::possiblyMissing< bool > cleanup;
};

struct RescanMetadataBody
{
	http::JsonDocument records;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/jobs/{job_id}/status"}]]
[[= http::Group{"Jobs"}]]
[[= http::Validation< http::validation::Integer< JobID > >{"job_id"}]]
http::HTTPTask< http::JsonResult< JobStatusResponse > > jobStatus( [[maybe_unused]] db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/jobs/status"}]]
[[= http::Group{"Jobs"}]]
http::HTTPTask< http::JsonResult< JobsStatusResponse > > jobsStatus( [[maybe_unused]] db::DBContext& db, http::Query< JobsStatusQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/jobs/metadata/rescan"}]]
[[= http::Group{"Jobs"}]]
[[= http::Input< RescanMetadataBody > {}]]
http::HTTPTask< http::JsonResult< QueuedJobResponse > > rescanMetadata( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[=http::Endpoint{proxygen::HTTPMethod::POST, "/jobs/urls/deduplicate"}]]
[[=http::Group{"Jobs"}]]
http::HTTPTask< http::JsonResult< QueuedJobResponse > >	queueUrlDeduplication( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
