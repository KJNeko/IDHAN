#pragma once

#include <cstdint>
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

struct HydrusServiceQuery
{
	http::possiblyMissing< std::string > service_key;
	http::possiblyMissing< std::string > service_name;
};

struct HydrusTagSearchQuery
{
	http::possiblyMissing< std::string > search;
	http::possiblyMissing< std::string > tag_display_type;
};

struct HydrusFileSearchQuery
{
	http::possiblyMissing< std::string > tags;
	http::possiblyMissing< std::uint64_t > file_sort_type;
	http::possiblyMissing< bool > file_sort_asc;
	http::possiblyMissing< bool > return_file_ids;
	http::possiblyMissing< bool > return_hashes;
	http::possiblyMissing< std::string > tag_display_type;
};

struct HydrusUrlInfoQuery
{
	http::possiblyMissing< std::string > url;
};

struct HydrusAddUrlBody
{
	std::string url;
};

struct HydrusAddedUrlResponse
{
	std::string human_result_text;
	std::string normalised_url;
};

struct HydrusUrlFileStatus
{
	std::int32_t status;
	std::string hash;
	std::string note;
};

struct HydrusUrlFilesResponse
{
	std::string normalised_url;
	std::vector< HydrusUrlFileStatus > url_file_statuses;
};

struct HydrusFileQuery
{
	http::possiblyMissing< RecordID > file_id;
	http::possiblyMissing< std::string > hash;
	http::possiblyMissing< bool > download;
	http::possiblyMissing< bool > regenerate;
	http::possiblyMissing< std::size_t > size;
};

[[nodiscard]] std::vector< http::RouteDefinition > hydrusRoutes( db::DBContext& db );

} // namespace idhan::httpapi
