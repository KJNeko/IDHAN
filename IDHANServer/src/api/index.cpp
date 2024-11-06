//
// Created by kj16609 on 11/6/24.
//

#ifndef IDHAN_VERSION
#define IDHAN_VERSION "SOURCE TESTBUILD"
#endif

#ifndef IDHAN_API_VERSION
#define IDHAN_API_VERSION "SOURCE TESTBUILD"
#endif

#ifndef HYDRUS_API_VERSION
#define HYDRUS_API_VERSION "SOURCE TESTBUILD"
#endif

#include <filesystem>
#include <fstream>

#include "drogon/HttpRequest.h"
#include "helpers/ResponseCallback.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

	/*
	void getIndex( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		log::debug( "/" );

		const auto working_dir { std::filesystem::current_path() };
		const auto filepath { working_dir / "pages" / "index.html" };

		if ( auto ifs = std::ifstream( filepath ); ifs )
		{
			std::vector< char > buffer {};
			buffer.resize( std::filesystem::file_size( filepath ) );

			ifs.read( buffer.data(), buffer.size() );

			auto response { drogon::HttpResponse::newHttpResponse() };

			response->setStatusCode( drogon::k200OK );
			response->setContentTypeCode( drogon::CT_TEXT_HTML );

			std::string_view str { buffer.data(), buffer.size() };

			response->setBody( std::string( str ) );

			callback( response );
		}
	}
	*/

	void getVersion( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		log::debug( "/version" );

		Json::Value json;
		json[ "idhan_version" ] = IDHAN_VERSION;
		json[ "idhan_api_version" ] = IDHAN_API_VERSION;
		json[ "hydrus_api_version" ] = HYDRUS_API_VERSION;

		callback( drogon::HttpResponse::newHttpJsonResponse( json ) );
	}

} // namespace idhan::api
