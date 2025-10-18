//
// Created by kj16609 on 10/18/25.
//

#include "api/APIMaintenance.hpp"
#include "paths.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::integrityCheck( drogon::HttpRequestPtr request )
{
	Json::Value root;

	root[ "static" ] = getStaticPath().string();
	root[ "static_exists" ] = std::filesystem::exists( getStaticPath() ) ? "true" : "false";

	for ( const auto& file : std::filesystem::recursive_directory_iterator( getStaticPath() ) )
	{
		if ( file.is_regular_file() ) root[ "files" ].append( file.path().string() );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

} // namespace idhan::api