//
// Created by kj16609 on 3/11/25.
//
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>
#pragma GCC diagnostic pop

#include <expected>
#include <vector>

#include "../../db/drogonArrayBind.hpp"
#include "IDHANTypes.hpp"

namespace idhan::api::helpers
{

drogon::Task< std::expected< std::filesystem::path, drogon::HttpResponsePtr > > getRecordPath(
	RecordID record_id,
	DbClientPtr db );

std::expected< TagDomainID, drogon::HttpResponsePtr > getTagDomainID( drogon::HttpRequestPtr request );

constexpr std::chrono::seconds default_max_age {
	std::chrono::duration_cast< std::chrono::seconds >( std::chrono::years( 1 ) )
};

void addFileCacheHeader( drogon::HttpResponsePtr sharedPtr, std::chrono::seconds max_age = default_max_age );

} // namespace idhan::api::helpers
