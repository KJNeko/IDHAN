//
// Created by kj16609 on 11/10/24.
//

#include <fstream>

#include "api/InfoAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "paths.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > InfoAPI::apiDocs( drogon::HttpRequestPtr request )
{
	const std::string path_str { request->getPath() };
	const std::filesystem::path file_path { std::filesystem::path( path_str.substr( 1 ) ).lexically_normal() };

	// the regex route passes the raw url path through to the filesystem; after
	// normalization any remaining ".." component escapes the static directory
	for ( const auto& part : file_path )
		if ( part == ".." ) co_return createBadRequest( "Invalid docs path: {}", path_str );

	const auto static_path { getStaticPath() };

	log::info( "Attempting to get api docs from {}", ( static_path / file_path ).string() );

	co_return drogon::HttpResponse::newFileResponse( static_path / file_path );
}

drogon::Task< drogon::HttpResponsePtr > InfoAPI::api( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	co_return drogon::HttpResponse::newFileResponse( getStaticPath() / "apidocs.html" );
}

} // namespace idhan::api
