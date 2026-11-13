#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
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

struct ParseMimeQuery
{
	http::possiblyMissing< std::string > filename;
};

struct[ [= http::SchemaName { "DatabaseSunburst" }] ] SunburstResponse
{
	struct SunburstIndex
	{
		std::string name;
		[[= http::Name { "value" }]][ [= http::Minimum { 0 }] ] std::int64_t size;
	};

	struct SunburstTable
	{
		std::string name;
		[[= http::Name { "value" }]][ [= http::Minimum { 0 }] ] std::int64_t size;
		[[= http::Name { "children" }]] std::vector< SunburstIndex > indexes;
	};

	std::string name;
	std::vector< SunburstTable > children;
};

struct[ [= http::SchemaName { "DatabaseStats" }] ] DatabaseStatsResponse
{
	[[= http::Minimum { 0 }]] std::int64_t records;
	[[= http::Minimum { 0 }]] std::int64_t tags;
	std::size_t clusters;
	[[= http::Minimum { 0 }]] std::int64_t mappings;
	bool mappings_estimated;
};

struct[ [= http::SchemaName { "IntegrityCheck" }] ] IntegrityCheckResponse
{
	[[= http::Name { "static" }]] std::string static_path;
	bool static_exists;
	std::vector< std::string > files;
};

struct[ [= http::SchemaName { "PurgeThumbnails" }] ] PurgeThumbnailsResponse
{
	bool success { true };
	std::uint64_t deleted_count;
	std::string message { "Thumbnails purged successfully" };
};

using MimeListResponse = std::map< std::string, std::string >;

struct[ [= http::SchemaName { "MimeMetadata" }] ] MimeMetadataResponse
{
	std::string name;
	http::possiblyMissing< std::size_t > count;
	http::possiblyMissing< std::size_t > uncompressed_size;
	[[= http::Description { "Metadata supplied by the selected parser module." }]] http::JsonDocument extra;
};

struct[ [= http::SchemaName { "MimeMetadataError" }] ] MimeMetadataErrorResponse
{
	http::possiblyMissing< std::string > name;
	std::string error;
};

struct[ [= http::SchemaName { "ParsedMime" }] ] ParsedMimeResponse
{
	bool success { true };
	std::string mime;
	[[= http::Minimum { 1 }]] MimeID mime_id;
	std::uint16_t generic_mime_id;
	std::vector< std::variant< MimeMetadataResponse, MimeMetadataErrorResponse > > metadata_modules;
};

struct[ [= http::SchemaName { "UnrecognizedMime" }] ] UnrecognizedMimeResponse
{
	bool success { false };
	std::string error { "Failed to parse mime type" };
};

using ParseMimeResponse = std::variant< ParsedMimeResponse, UnrecognizedMimeResponse >;

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/db/stats/sunburst"}]]
[[= http::Group{"Maintenance"}]]
http::HTTPTask< http::JsonResult< SunburstResponse > > sunburst( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/db/stats"}]]
[[= http::Group{"Maintenance"}]]
http::HTTPTask< http::JsonResult< DatabaseStatsResponse > > databaseStats( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/integrity"}]]
[[= http::Group{"Maintenance"}]]
[[= http::Output< http::ErrorResponse >{500}]]
http::HTTPTask< http::JsonResult< IntegrityCheckResponse > > integrityCheck( [[maybe_unused]] db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/purge/thumbnails"}]]
[[= http::Group{"Maintenance"}]]
[[= http::Output< http::ErrorResponse >{500}]]
http::HTTPTask< http::JsonResult< PurgeThumbnailsResponse > > purgeThumbnails( [[maybe_unused]] db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/mime"}]]
[[= http::Group{"Maintenance"}]]
http::HTTPTask< http::JsonResult< MimeListResponse > > listMimes( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/mime/parse"}]]
[[= http::Group{"Maintenance"}]]
[[= http::Input< http::RequestBody >{"application/octet-stream"}]]
[[= http::Output< http::ErrorResponse >{400}]]
http::HTTPTask< http::JsonResult< ParseMimeResponse > > parseMime( [[maybe_unused]] db::DBContext& db, http::RequestContext context, http::Query< ParseMimeQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/mime/generate_thumbnail"}]]
[[= http::Group{"Maintenance"}]]
[[= http::Input< http::RequestBody >{"application/octet-stream"}]]
[[= http::Output< http::ErrorResponse >{400}]]
[[= http::Output< http::ErrorResponse >{404}]]
[[= http::Output< http::ErrorResponse >{500}]]
http::HTTPTask< http::EncodedResult< http::RequestBody, "image/webp" > > createThumbnail( [[maybe_unused]] db::DBContext& db, http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
