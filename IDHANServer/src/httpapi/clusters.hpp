#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/response/ErrorResponse.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"
#include "idhan/http/json/possiblyMissing.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct ClusterBodySize
{
	[[= http::Minimum { 0 }]] http::possiblyMissing< std::int64_t > limit;
};

struct AddClusterBody
{
	std::string name;
	std::string path;
	[[= http::Minimum { 0 }]] http::possiblyMissing< std::int16_t > ratio;
	http::possiblyMissing< bool > readonly;
	http::possiblyMissing< ClusterBodySize > size;
};

struct ModifyClusterBody
{
	http::possiblyMissing< std::string > name;
	http::possiblyMissing< std::string > path;
	[[= http::Minimum { 0 }]] http::possiblyMissing< std::int16_t > ratio;
	http::possiblyMissing< bool > readonly;
	http::possiblyMissing< ClusterBodySize > size;
};

struct ScanClusterQuery
{
	http::possiblyMissing< bool > verify_hash;
	http::possiblyMissing< bool > readonly;
	http::possiblyMissing< bool > scan_mime;
	http::possiblyMissing< bool > rescan_mime;
	http::possiblyMissing< bool > scan_metadata;
	http::possiblyMissing< bool > rescan_metadata;
	http::possiblyMissing< bool > stop_on_fail;
	http::possiblyMissing< bool > adopt_orphans;
	http::possiblyMissing< bool > remove_missing_files;
	http::possiblyMissing< bool > fix_extensions;
	[[= http::Minimum { 0 }]][ [= http::Maximum { 64 }] ] http::possiblyMissing< std::size_t > concurrency;
};

struct[ [= http::SchemaName { "ClusterInfo" }] ] ClusterInfoResponse
{
	struct Size
	{
		[[= http::Minimum { 0 }]] std::int64_t used;
		[[= http::Description {
			"Storage limit in bytes; zero means unlimited." }]][ [= http::Minimum { 0 }] ] std::int64_t limit;
		[[= http::Description {
			"Remaining bytes, clamped to zero when usage reaches the limit. Unlimited clusters report the maximum signed 64-bit integer." }]]
			[ [= http::Minimum { 0 }] ] std::int64_t available;
	};

	struct MimeUsage
	{
		std::optional< std::string > mime;
		[[= http::Minimum { 0 }]] std::int64_t count;
		[[= http::Minimum { 0 }]] std::int64_t bytes;
	};

	[[= http::Minimum { 1 }]] ClusterID cluster_id;
	bool readonly;
	std::string name;
	Size size;
	[[= http::Minimum { 0 }]] std::int64_t file_count;
	[[= http::Minimum { 0 }]] std::int16_t ratio_number;
	std::string path;
	std::vector< MimeUsage > by_mime;
};

struct DeletedClusterResponse
{};

struct ClusterScanResponse
{
	JobID job_id;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/clusters/{cluster_id}/info"}]]
[[= http::Group{"Clusters"}]]
[[= http::Validation< http::validation::PositiveInteger< ClusterID > >{"cluster_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< ClusterInfoResponse > > clusterInfo( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/clusters/list"}]]
[[= http::Group{"Clusters"}]]
http::HTTPTask< http::JsonResult< std::vector< ClusterInfoResponse > > > listClusters( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/clusters/add"}]]
[[= http::Group{"Clusters"}]]
[[= http::Output< http::ErrorResponse >{409}]]
http::HTTPTask< http::JsonResult< ClusterInfoResponse, 201 > > addCluster( db::DBContext& db, http::JsonBody< AddClusterBody > body );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::PATCH, "/clusters/{cluster_id}/modify"}]]
[[= http::Group{"Clusters"}]]
[[= http::Validation< http::validation::PositiveInteger< ClusterID > >{"cluster_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
[[= http::Output< http::ErrorResponse >{409}]]
[[= http::Output< http::ErrorResponse >{503}]]
http::HTTPTask< http::JsonResult< ClusterInfoResponse > > modifyCluster( db::DBContext& db, http::RequestContext context, http::JsonBody< ModifyClusterBody > body );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::DELETE, "/clusters/{cluster_id}/remove"}]]
[[= http::Group{"Clusters"}]]
[[= http::Validation< http::validation::PositiveInteger< ClusterID > >{"cluster_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< DeletedClusterResponse > > deleteCluster( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/clusters/{cluster_id}/scan"}]]
[[= http::Group{"Clusters"}]]
[[= http::Validation< http::validation::PositiveInteger< ClusterID > >{"cluster_id"}]]
[[= http::Output< http::ErrorResponse >{404}]]
[[= http::Output< http::ErrorResponse >{503}]]
http::HTTPTask< http::JsonResult< ClusterScanResponse > > scanCluster( db::DBContext& db, http::RequestContext context, http::Query< ScanClusterQuery > query );
// clang-format on

} // namespace idhan::httpapi
