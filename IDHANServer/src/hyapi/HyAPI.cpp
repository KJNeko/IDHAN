//
// Created by kj16609 on 11/6/24.
//

#include "HyAPI.hpp"

#include "constants/hydrus_version.hpp"
#include "fixme.hpp"
#include "versions.hpp"

namespace idhan::hyapi
{

	void HydrusAPI::unsupported( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		Json::Value root;
		root[ "status" ] = 410;
		root[ "message" ] = "IDHAN Hydrus API does not support this request";
	}

	// /hyapi/api_version
	void HydrusAPI::apiVersion( [[maybe_unused]] const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		Json::Value json;
		json[ "version" ] = HYDRUS_MIMICED_API_VERSION;
		json[ "hydrus_version" ] = HYDRUS_MIMICED_VERSION;

		// I'm unsure if anything would actually ever need this.
		// But i figured i'd supply it anyways
		json[ "idhan_server_version" ] = IDHAN_VERSION;
		json[ "idhan_api_version" ] = IDHAN_API_VERSION;

		const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

		callback( response );
	}

	// /hyapi/access/request_new_permissions
	void HydrusAPI::requestNewPermissions( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		idhan::fixme();
	}

	// /hyapi/access/session_key
	void HydrusAPI::sessionKey( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		idhan::fixme();
	}

	// /hyapi/access/verify_access_key
	void HydrusAPI::verifyAccessKey( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		Json::Value json;
		json[ "basic_permissions" ] = 0;
		json[ "human_description" ] = "";

		const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

		callback( response );
	}

	void HydrusAPI::getService( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::getServices( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::addFile( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::searchFiles( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::fileHashes( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::fileMetadata( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::file( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

	void HydrusAPI::thumbnail( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{}

} // namespace idhan::hyapi
