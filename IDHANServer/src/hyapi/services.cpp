//
// Created by kj16609 on 7/23/24.
//

#include "drogon/HttpAppFramework.h"
#include "fixme.hpp"

namespace idhan::hyapi
{

	using ResponseFunction = std::function< void( const drogon::HttpResponsePtr& ) >;

	// /hyapi/services/get_service
	void getServiceFromName(
		const drogon::HttpRequestPtr& request,
		ResponseFunction&& callback,
		const std::string& service_name,
		const std::string& service_key )
	{
		std::cout << std::format( "Name: {}\nKey: {}", service_name, service_key ) << std::endl;
		fixme();
	}

	// /hyapi/services/get_services
	void getServices( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		fixme();
	}

	void setupServiceHandlers()
	{
		auto& app = drogon::app();
		app.registerHandler(
			"/hyapi/get_service?service_name={service_name}&service_key={service_key}", &getServiceFromName );
		//app.registerHandler( "/hyapi/get_service?service_key={service_key}", &getServiceFromKey );
		app.registerHandler( "/hyapi/get_services", &getServices );
		//app.registerHandler( "/hyapi/access/session_key", &getSessionKey );
		//app.registerHandler( "/hyapi/access/verify_access_key", &getVerifyAccessKey );
	}

} // namespace idhan::hyapi
