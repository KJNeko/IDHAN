//
// Created by kj16609 on 11/10/24.
//

#include <fstream>

#include "IDHANApi.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

void IDHANApi::apiDocs( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
{
	std::string path { request->getPath() };
	log::info( path );

	callback( drogon::HttpResponse::newFileResponse( "./pages" + path ) );
}

void IDHANApi::api( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
{
	if ( auto ifs = std::ifstream( "./pages/apidocs.html", std::ios::ate ); ifs )
	{
		std::size_t size { ifs.tellg() };
		ifs.seekg( 0, std::ios::beg );

		std::string str {};
		str.resize( size );
		ifs.read( str.data(), str.size() );

		// create http response
		auto response { drogon::HttpResponse::newHttpResponse() };
		response->setContentTypeCode( drogon::ContentType::CT_TEXT_HTML );

		response->setBody( str );

		callback( response );
	}

	callback( drogon::HttpResponse::newHttpResponse() );
}

} // namespace idhan::api
