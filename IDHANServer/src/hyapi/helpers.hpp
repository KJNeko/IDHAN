#pragma once

#include "IDHANTypes.hpp"
#include "crypto/SHA256.hpp"
#include "db/dbTypes.hpp"

namespace idhan::hyapi::helpers
{

[[nodiscard]] drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	extractRecordIDsFromFilesJson( Json::Value json, DbClientPtr db );

[[nodiscard]] drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	extractRecordIDsFromParameters( drogon::HttpRequestPtr request, DbClientPtr db );

[[nodiscard]] drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > > extractRecordIDsToJsonFromFiles(
	Json::Value json,
	DbClientPtr db );

[[nodiscard]] std::string extractHttpResponseErrorMessage( const drogon::HttpResponsePtr response );

//! Hydrus `ext` values include the leading dot; IDHAN's stored extensions do not.
[[nodiscard]] std::string withLeadingDot( std::string_view extension );

} // namespace idhan::hyapi::helpers
