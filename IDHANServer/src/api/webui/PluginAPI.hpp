#pragma once

#include <drogon/HttpController.h>

#include "api/APIAuth.hpp"

namespace idhan::api
{

//! WebUI plugin discovery (M6). Plugin bundles live under `<static>/plugins/<dir>/` (see getPluginsPath)
//! and are served to the browser by the ordinary static file router; this endpoint just publishes the
//! index. The bundles themselves are loaded client-side via a dynamic import — the server never runs
//! plugin code (this is not the C++ ModuleLoader path).
class PluginAPI : public drogon::HttpController< PluginAPI >
{
	//! Returns the validated plugin index (an array of manifests with resolved bundle URLs). The scan
	//! result is cached in memory; pass `?rescan=true` to force a fresh directory scan, mirroring
	//! `/mime/reload`.
	static drogon::Task< drogon::HttpResponsePtr > listPlugins( drogon::HttpRequestPtr req );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( PluginAPI::listPlugins, "/plugins", drogon::Get, IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
