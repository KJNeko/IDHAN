#pragma once

#include "IDHANTypes.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/orm/DbClient.h"

namespace idhan::hyapi::helpers
{

/**
 * @param json Converts a hydrus `files` json array to contain a array of record_ids
 */
[[nodiscard]] drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	extractRecordIDsFromFilesJson( Json::Value json, DbClientPtr db );

[[nodiscard]] drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	extractRecordIDsFromParameters( drogon::HttpRequestPtr request, DbClientPtr db );

[[nodiscard]] drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > > extractRecordIDsToJsonFromFiles(
	Json::Value json,
	DbClientPtr db );

[[nodiscard]] std::string extractHttpResponseErrorMessage( const drogon::HttpResponsePtr response );

//! IDHAN stores file extensions without a leading dot (e.g. "jpg"), but the Hydrus API's `ext`
//! field is expected to have one (e.g. ".jpg"). Leaves an empty extension untouched.
[[nodiscard]] std::string withLeadingDot( std::string_view extension );

} // namespace idhan::hyapi::helpers
