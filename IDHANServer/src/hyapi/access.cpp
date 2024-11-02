//
// Created by kj16609 on 7/23/24.
//

#include "../versions.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "drogon/HttpAppFramework.h"
#include "fixme.hpp"

namespace idhan::hyapi
{

	constexpr auto PRETEND_API_VERSION { 65 };
	constexpr auto PRETEND_HYDRUS_VERISON { 583 };


	// /hyapi/api_version
	void getApiVersion( [[maybe_unused]] const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		Json::Value json;
		json[ "version" ] = PRETEND_API_VERSION;
		json[ "hydrus_version" ] = PRETEND_HYDRUS_VERISON;

		// I'm unsure if anything would actually ever need this.
		// But i figured i'd supply it anyways
		json[ "idhan_server_version" ] = IDHAN_SERVER_VERSION;
		json[ "idhan_api_version" ] = IDHAN_API_VERSION;

		const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

		callback( response );
	}

	// /hyapi/access/request_new_permissions
	void getRequestNewPermissions(
		const drogon::HttpRequestPtr& request,
		ResponseFunction&& callback,
		const std::string& name,
		const std::vector< int >& permissions )
	{
		fixme();
	}

	// /hyapi/access/session_key
	void getSessionKey(
		const drogon::HttpRequestPtr& request, ResponseFunction&& callback, const std::string& access_key )
	{
		fixme();
	}

	// In order to get the access key we also need to check if it's in the header given to us.
	std::string getAccessKey( const drogon::HttpRequestPtr& request, const std::string& string )
	{
		if ( string.empty() )
			return request->getHeader( "Hydrus-Client-API-Access-Key" );
		else
			return string;
	}

	// /hyapi/access/verify_access_key
	void getVerifyAccessKey(
		const drogon::HttpRequestPtr& request, ResponseFunction&& callback, const std::string& access_key )
	{
		std::string auth_key { getAccessKey( request, access_key ) };

		Json::Value json;
		json[ "basic_permissions" ] = 0;
		json[ "human_description" ] = "";

		const auto response { drogon::HttpResponse::newHttpJsonResponse( json ) };

		callback( response );
	}

	void setupAccessHandlers()
	{
		auto& app = drogon::app();
		app.registerHandler( "/hyapi/api_version", &getApiVersion );
		app.registerHandler(
			"/hyapi/access/request_new_permissions?name={name}&basic_permissions={permissions}",
			&getRequestNewPermissions );
		app.registerHandler( "/hyapi/session_key?Hydrus-Client-API-Access-Key={access_key}", &getSessionKey );
		app.registerHandler(
			"/hyapi/verify_access_key?Hydrus-Client-API-Access-Key={access_key}", &getVerifyAccessKey );
	}

} // namespace idhan::hyapi
