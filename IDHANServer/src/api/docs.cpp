//
// Created by kj16609 on 11/10/24.
//

#include <fstream>

#include "InfoAPI.hpp"
#include "helpers/createBadRequest.hpp"
#include "paths.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > InfoAPI::apiDocs( drogon::HttpRequestPtr request )
{
	const std::string path_str { request->getPath() };
	const std::filesystem::path file_path { path_str.substr( 1 ) };
	const auto static_path { getStaticPath() };

	log::info( "Attempting to get api docs from {}", ( static_path / file_path ).string() );

	co_return drogon::HttpResponse::newFileResponse( static_path / file_path );
}

drogon::Task< drogon::HttpResponsePtr > InfoAPI::api( drogon::HttpRequestPtr request )
{
	co_return drogon::HttpResponse::newFileResponse( getStaticPath() / "apidocs.html" );
}

} // namespace idhan::api
