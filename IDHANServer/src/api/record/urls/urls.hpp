//
// Created by kj16609 on 7/24/25.
//
#pragma once
#include <expected>

#include "IDHANTypes.hpp"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"

namespace idhan::helpers
{

drogon::Task< std::expected< std::vector< std::string >, drogon::HttpResponsePtr > >
	fetchUrlsStrings( RecordID record_id, drogon::orm::DbClientPtr db );

drogon::Task< std::expected< Json::Value, drogon::HttpResponsePtr > >
	fetchUrlsJson( RecordID record_id, drogon::orm::DbClientPtr db );

} // namespace idhan::helpers

namespace idhan
{
using namespace helpers;
}