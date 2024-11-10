//
// Created by kj16609 on 11/10/24.
//

#include "IDHANApi.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

void IDHANApi::api( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
{
	std::string path { request->getPath() };
	log::info( path );

	callback( drogon::HttpResponse::newFileResponse( "./pages/api.yaml" ) );
}

} // namespace idhan::api
