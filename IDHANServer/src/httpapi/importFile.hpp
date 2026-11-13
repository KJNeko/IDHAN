#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct UnknownImportMimeResponse
{
	std::string reason;
	std::uint8_t reason_id;
	std::uint8_t status;
};

struct DeletedImportResponse
{
	RecordID record_id;
	std::optional< std::int64_t > cluster_delete_time;
	std::uint8_t status;
};

struct ImportedRecordResponse
{
	struct Record
	{
		RecordID id;
	};

	struct File
	{
		std::optional< std::string > import_time_human {};
		std::optional< std::int64_t > import_time {};
		std::optional< std::string > deleted_time_human {};
		std::optional< std::int64_t > deleted_time {};
	};

	std::uint8_t status;
	RecordID record_id;
	Record record;
	File file;
};

using ImportFileResponse = std::variant< UnknownImportMimeResponse, DeletedImportResponse, ImportedRecordResponse >;

struct ImportRecordFileQuery
{
	http::possiblyMissing< bool > force_import;
	http::possiblyMissing< std::string > filename;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/file/import"}]]
[[= http::Group{"Files"}]]
[[= http::Input< http::RequestBody >{"application/octet-stream"}]]
http::HTTPTask< http::JsonResult< ImportFileResponse > > importRecordFile( db::DBContext& db, http::RequestContext context, http::Query< ImportRecordFileQuery > query );
// clang-format on

} // namespace idhan::httpapi
