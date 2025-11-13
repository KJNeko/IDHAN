//
// Created by kj16609 on 7/24/25.
//
#pragma once

#include <drogon/HttpResponse.h>

#include <expected>
#include <string>

#include "threading/ExpectedTask.hpp"
#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"

namespace idhan::helpers
{
constexpr UrlID INVALID_URL_ID { 0 };

ExpectedTask< UrlID > findOrCreateUrl( std::string url, DbClientPtr db );

ExpectedTask< UrlDomainID > findOrCreateUrlDomain( std::string url, DbClientPtr db );
} // namespace idhan::helpers
