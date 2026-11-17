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

struct SearchRecordsResponse
{
	struct Step
	{
		std::string step;
		std::int64_t rows;
		bool inverted;
		std::string kind;
		http::possiblyMissing< std::int64_t > micros {};
	};

	http::possiblyMissing< std::vector< RecordID > > record_ids {};
	http::possiblyMissing< std::vector< std::string > > hashes {};
	std::int64_t count {};
	bool truncated {};
	std::int64_t query_ms {};
	http::possiblyMissing< std::vector< Step > > stats {};
};

struct SearchParameters
{
	[[= http::Minimum { 1 }]] std::vector< TagID > tag_ids {};
	[[= http::Minimum { 1 }]] std::vector< TagID > tag_id {};
	[[= http::Minimum { 1 }]] std::vector< TagDomainID > tag_domains {};
	http::possiblyMissing< std::string > by;
	http::possiblyMissing< std::string > order;
	http::possiblyMissing< bool > include_deleted;
};

struct SearchPostBodySort
{
	http::JsonDocument by;
	http::JsonDocument order;
};

struct SearchPostBody
{
	http::JsonDocument debug;
	http::JsonDocument display;
	http::JsonDocument include_deleted;
	http::JsonDocument limit;
	http::JsonDocument offset;

	[[= http::Name { "return" }]] http::JsonDocument return_fields;
	SearchPostBodySort sort;
	http::JsonDocument tag_domains;
	http::JsonDocument tag_ids;
	http::JsonDocument tags;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/search"}]]
[[= http::Group{"Search"}]]
[[nodiscard]] http::HTTPTask< http::JsonResult< std::vector< RecordID > > > searchRecords( db::DBContext& db, http::Query< SearchParameters > parameters );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/search"}]]
[[= http::Group{"Search"}]]
[[= http::Input< SearchPostBody > {}]]
[[nodiscard]] http::HTTPTask< http::JsonResult< SearchRecordsResponse > > searchPost( db::DBContext& db, http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
