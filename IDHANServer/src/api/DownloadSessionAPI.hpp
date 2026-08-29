#pragma once

#include <drogon/HttpController.h>

#include "api/APIAuth.hpp"

namespace idhan::api
{
class DownloadSessionAPI final : public drogon::HttpController< DownloadSessionAPI >
{
	static drogon::Task< drogon::HttpResponsePtr > list( drogon::HttpRequestPtr request );
	static drogon::Task< drogon::HttpResponsePtr > create( drogon::HttpRequestPtr request );
	static drogon::Task< drogon::HttpResponsePtr > get( drogon::HttpRequestPtr request, std::string session_id );
	static drogon::Task< drogon::HttpResponsePtr > destroy( drogon::HttpRequestPtr request, std::string session_id );
	static drogon::Task< drogon::HttpResponsePtr > submitUrl( drogon::HttpRequestPtr request, std::string session_id );
	static drogon::Task< drogon::HttpResponsePtr > submitUrlSession( drogon::HttpRequestPtr request );
	static drogon::Task< drogon::HttpResponsePtr > urls( drogon::HttpRequestPtr request, std::string session_id );
	static drogon::Task< drogon::HttpResponsePtr > retryUrl(
		drogon::HttpRequestPtr request,
		std::string session_id,
		std::string url_id );
	static drogon::Task< drogon::HttpResponsePtr > records( drogon::HttpRequestPtr request, std::string session_id );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( DownloadSessionAPI::list, "/download_sessions", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( DownloadSessionAPI::create, "/download_sessions", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( DownloadSessionAPI::get, "/download_sessions/{session_id}", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( DownloadSessionAPI::destroy, "/download_sessions/{session_id}", drogon::Delete, IDHANAPIAuthName );
	ADD_METHOD_TO( DownloadSessionAPI::submitUrlSession, "/download_sessions/urls", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO(
		DownloadSessionAPI::submitUrl,
		"/download_sessions/{session_id}/urls",
		drogon::Post,
		IDHANAPIAuthName );
	ADD_METHOD_TO( DownloadSessionAPI::urls, "/download_sessions/{session_id}/urls", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO(
		DownloadSessionAPI::retryUrl,
		"/download_sessions/{session_id}/urls/{url_id}/retry",
		drogon::Post,
		IDHANAPIAuthName );
	ADD_METHOD_TO(
		DownloadSessionAPI::records,
		"/download_sessions/{session_id}/records",
		drogon::Get,
		IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
