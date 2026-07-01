//
// Created by kj16609 on 10/18/25.
//

#include "api/APIMaintenance.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "paths.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > APIMaintenance::integrityCheck(
	[[maybe_unused]] drogon::HttpRequestPtr request )
{
	Json::Value root;

	const auto static_path { getStaticPath() };
	root[ "static" ] = static_path.string();
	root[ "static_exists" ] = std::filesystem::exists( static_path );

	const auto scan_root { static_path.parent_path() };
	if ( std::filesystem::exists( scan_root ) )
	{
		try
		{
			for ( const auto& file : std::filesystem::recursive_directory_iterator( scan_root ) )
			{
				if ( file.is_regular_file() ) root[ "files" ].append( file.path().string() );
			}
		}
		catch ( const std::filesystem::filesystem_error& e )
		{
			co_return createInternalError( "Failed to iterate static directory: {}", e.what() );
		}
	}
	else
	{
		root[ "files" ] = Json::Value( Json::arrayValue );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( root );
}

} // namespace idhan::api
