//
// Created by kj16609 on 7/24/25.
//
#pragma once
#include <expected>
#include <string>

#include "IDHANTypes.hpp"
#include "drogon/HttpResponse.h"
#include "drogon/orm/DbClient.h"

namespace idhan::helpers
{

constexpr UrlID INVALID_URL_ID { 0 };

drogon::Task< std::expected< UrlID, drogon::HttpResponsePtr > >
	findOrCreateUrl( const std::string url, drogon::orm::DbClientPtr db );

} // namespace idhan::helpers