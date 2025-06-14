//
// Created by kj16609 on 3/22/25.
//

#include "helpers.hpp"

namespace idhan::api::helpers
{

void addFileCacheHeader( drogon::HttpResponsePtr ptr, std::chrono::seconds max_age )
{
	ptr->addHeader( "Cache-Control", std::format( "private, immutable, max-age={}", max_age.count() ) );
}

} // namespace idhan::api::helpers
