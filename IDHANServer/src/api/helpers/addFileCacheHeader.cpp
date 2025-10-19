//
// Created by kj16609 on 3/22/25.
//

#include "drogon/HttpResponse.h"
#include "helpers.hpp"
#include "logging/format_ns.hpp"

namespace idhan::api::helpers
{

void addFileCacheHeader( drogon::HttpResponsePtr ptr, std::chrono::seconds max_age )
{
	ptr->addHeader( "Cache-Control", format_ns::format( "private, immutable, max-age={}", max_age.count() ) );
}

} // namespace idhan::api::helpers
