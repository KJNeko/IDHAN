#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include "IDHANTypes.hpp"
#include "idhan/http/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::RouteDefinition recordInfoRoute( db::DBContext& db, std::string path );

[[nodiscard]] http::RouteDefinition recordMetadataBatchRoute( db::DBContext& db );

[[nodiscard]] http::RouteDefinition recordMetadataScanRoute( db::DBContext& db );

[[nodiscard]] http::HTTPTask< std::expected< void, http::Response > > parseAndStoreRecordMetadata(
	db::DBContext& db,
	RecordID record_id );

[[nodiscard]] http::HTTPTask< std::expected< std::filesystem::path, http::Response > > recordFilePathForScan(
	db::DBContext& db,
	RecordID record_id );

} // namespace idhan::httpapi
