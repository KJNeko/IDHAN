#pragma once

#include <drogon/HttpController.h>

#include "api/APIAuth.hpp"

namespace idhan::api
{
class DownloaderSecretsAPI final : public drogon::HttpController< DownloaderSecretsAPI >
{
	static drogon::Task< drogon::HttpResponsePtr > list( drogon::HttpRequestPtr request );
	static drogon::Task< drogon::HttpResponsePtr > set( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( DownloaderSecretsAPI::list, "/downloader/secrets", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( DownloaderSecretsAPI::set, "/downloader/secrets", drogon::Post, IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
