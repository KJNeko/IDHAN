//
// Created by kj16609 on 3/22/25.
//

#include "helpers.hpp"

namespace idhan::api::helpers
{

void addFileCacheHeader( drogon::HttpResponsePtr ptr )
{
	ptr->addHeader( "Cache-control", "immutable, max-age=31536000" );
}

} // namespace idhan::api::helpers
