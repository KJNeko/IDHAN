//
// Created by kj16609 on 3/11/25.
//
#pragma once

#include <drogon/HttpResponse.h>
#include <drogon/utils/coroutine.h>

#include <expected>
#include <vector>

#include "../../db/drogonArrayBind.hpp"
#include "IDHANTypes.hpp"

namespace idhan::api::helpers
{

std::expected< TagDomainID, drogon::HttpResponsePtr > getTagDomainIDParameter( const drogon::HttpRequestPtr& request );

constexpr std::chrono::seconds default_max_age {
	std::chrono::duration_cast< std::chrono::seconds >( std::chrono::years( 1 ) )
};

void addFileCacheHeader( drogon::HttpResponsePtr sharedPtr, std::chrono::seconds max_age = default_max_age );

} // namespace idhan::api::helpers
