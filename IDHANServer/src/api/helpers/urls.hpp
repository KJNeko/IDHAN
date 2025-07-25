//
// Created by kj16609 on 7/24/25.
//
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#include <drogon/HttpResponse.h>
#include <drogon/orm/DbClient.h>
#pragma GCC diagnostic pop

#include <expected>
#include <string>

#include "IDHANTypes.hpp"

namespace idhan::helpers
{

constexpr UrlID INVALID_URL_ID { 0 };

drogon::Task< std::expected< UrlID, drogon::HttpResponsePtr > >
	findOrCreateUrl( std::string url, drogon::orm::DbClientPtr db );

} // namespace idhan::helpers