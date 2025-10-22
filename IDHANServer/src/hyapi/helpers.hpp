//
// Created by kj16609 on 7/24/25.
//
#pragma once

#include "IDHANTypes.hpp"
#include "crypto/SHA256.hpp"
#include "drogon/orm/DbClient.h"

namespace idhan::hyapi::helpers
{

/**
 * @brief
 * @param json Converts a hydrus `files` json array to contain a array of record_ids
 * @param db
 * @return
 */
drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	extractRecordIDsFromFilesJson( Json::Value json, drogon::orm::DbClientPtr db );

drogon::Task< std::expected< std::vector< RecordID >, drogon::HttpResponsePtr > >
	extractRecordIDsFromParameters( drogon::HttpRequestPtr request, drogon::orm::DbClientPtr db );

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	extractRecordIDsToJsonFromFiles( Json::Value json, drogon::orm::DbClientPtr db );

std::string extractIDHANHTTPError( const drogon::HttpResponsePtr response );

} // namespace idhan::hyapi::helpers