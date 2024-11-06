//
// Created by kj16609 on 11/6/24.
//
#pragma once
#include "HyAPIAuth.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "drogon/HttpController.h"

namespace idhan::hyapi
{
	class HydrusAPI : public drogon::HttpController< HydrusAPI >
	{
		void unsupported( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

		// Access management (access)
		void apiVersion( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
		void requestNewPermissions( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
		void sessionKey( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
		void verifyAccessKey( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
		void getService( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
		void getServices( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

		// Importing and deleting files (import)
		void addFile( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

		// Searching and Fetching files (search)
		void searchFiles( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
		void fileHashes( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
		void fileMetadata( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
		void file( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
		void thumbnail( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

	  public:

		METHOD_LIST_BEGIN

		// Access management
		ADD_METHOD_TO( HydrusAPI::apiVersion, "/hyapi/api_version", drogon::Get );
		ADD_METHOD_TO( HydrusAPI::requestNewPermissions, "/hyapi/request_new_permissions", drogon::Get, HyAPIAuthName );
		ADD_METHOD_TO( HydrusAPI::sessionKey, "/hyapi/session_key", drogon::Get, HyAPIAuthName );
		ADD_METHOD_TO( HydrusAPI::verifyAccessKey, "/hyapi/verify_access_key", drogon::Get, HyAPIAuthName );
		ADD_METHOD_TO( HydrusAPI::getService, "/hyapi/get_service", drogon::Get, HyAPIAuthName );
		ADD_METHOD_TO( HydrusAPI::getServices, "/hyapi/get_services", drogon::Get, HyAPIAuthName );

		// Importing and deleting files
		ADD_METHOD_TO( HydrusAPI::addFile, "/add_files/add_file", drogon::Get, HyAPIAuthName );

		// Searching and fetching files
		ADD_METHOD_TO( HydrusAPI::searchFiles, "/hyapi/get_files/search_files", drogon::Get, HyAPIAuthName );
		ADD_METHOD_TO( HydrusAPI::fileHashes, "/hyapi/get_files/file_hashes", drogon::Get, HyAPIAuthName );
		ADD_METHOD_TO( HydrusAPI::fileMetadata, "/hyapi/get_files/file_metadata", drogon::Get, HyAPIAuthName );
		ADD_METHOD_TO( HydrusAPI::file, "/hyapi/get_files/file", drogon::Get, HyAPIAuthName );
		ADD_METHOD_TO( HydrusAPI::thumbnail, "/hyapi/get_files/thumbnail", drogon::Get, HyAPIAuthName );
		ADD_METHOD_TO( HydrusAPI::unsupported, "/hyapi/get_files/file_path", drogon::Get, HyAPIAuthName ); // UNSUPPORTED
		ADD_METHOD_TO( HydrusAPI::unsupported, "/hyapi/get_files/thumbnail_path", drogon::Get, HyAPIAuthName ); // UNSUPPORTED

		METHOD_LIST_END
	};
} // namespace idhan::hyapi