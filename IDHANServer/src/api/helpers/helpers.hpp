//
// Created by kj16609 on 3/11/25.
//
#pragma once

#include <expected>

#include "IDHANTypes.hpp"
#include "drogon/HttpResponse.h"

namespace idhan::api::helpers
{

std::expected< TagDomainID, drogon::HttpResponsePtr > getTagDomainID( drogon::HttpRequestPtr request );

void addFileCacheHeader( drogon::HttpResponsePtr sharedPtr );

} // namespace idhan::api::helpers
