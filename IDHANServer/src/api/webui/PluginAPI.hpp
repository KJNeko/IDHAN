#pragma once

#include <drogon/HttpController.h>

#include "api/APIAuth.hpp"

namespace idhan::api
{

//! Publishes the WebUI plugin index; bundles are static client-side imports, not C++ modules.
class PluginAPI : public drogon::HttpController< PluginAPI >
{
	//! Cached unless `?rescan=true`.
	static drogon::Task< drogon::HttpResponsePtr > listPlugins( drogon::HttpRequestPtr req );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( PluginAPI::listPlugins, "/plugins", drogon::Get, IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
