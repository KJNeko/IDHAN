#pragma once

#include <drogon/HttpController.h>

#include "api/APIAuth.hpp"

namespace idhan::api
{
class DownloaderDebugAPI final : public drogon::HttpController< DownloaderDebugAPI >
{
	static drogon::Task< drogon::HttpResponsePtr > debug( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( DownloaderDebugAPI::debug, "/downloader/debug", drogon::Get, IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
