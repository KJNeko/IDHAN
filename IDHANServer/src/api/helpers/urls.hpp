//
// Created by kj16609 on 7/24/25.
//
#pragma once

#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>

#include <expected>
#include <string>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"

namespace idhan::helpers
{
constexpr UrlID INVALID_URL_ID { 0 };

drogon::Task< std::expected< UrlID, drogon::HttpResponsePtr > > findOrCreateUrl( std::string url, DbClientPtr db );
} // namespace idhan::helpers
