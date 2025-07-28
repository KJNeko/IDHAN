//
// Created by kj16609 on 11/10/24.
//

#include <fstream>

#include "IDHANApi.hpp"
#include "helpers/createBadRequest.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

void IDHANApi::apiDocs( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
{
	const std::string path { request->getPath() };

	// log::info( "Attempted to get {}", path );

	callback( drogon::HttpResponse::newFileResponse( "./static" + path ) );
}

void IDHANApi::api( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
{
	callback( drogon::HttpResponse::newFileResponse( "./static/apidocs.html" ) );
}

} // namespace idhan::api
