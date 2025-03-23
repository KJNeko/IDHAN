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
	drogon::Task< drogon::HttpResponsePtr > unsupported( const drogon::HttpRequestPtr& request );

	// Access management (access)
	drogon::Task< drogon::HttpResponsePtr > apiVersion( const drogon::HttpRequestPtr& request );
	drogon::Task< drogon::HttpResponsePtr > requestNewPermissions( const drogon::HttpRequestPtr& request );
	drogon::Task< drogon::HttpResponsePtr > sessionKey( const drogon::HttpRequestPtr& request );
	drogon::Task< drogon::HttpResponsePtr > verifyAccessKey( const drogon::HttpRequestPtr& request );
	drogon::Task< drogon::HttpResponsePtr > getService( const drogon::HttpRequestPtr& request );
	drogon::Task< drogon::HttpResponsePtr > getServices( const drogon::HttpRequestPtr& request );

	// Importing and deleting files (import)
	drogon::Task< drogon::HttpResponsePtr > addFile( const drogon::HttpRequestPtr& request );

	// Searching and Fetching files (search)
	drogon::Task< drogon::HttpResponsePtr > searchFiles( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > fileHashes( const drogon::HttpRequestPtr& request );
	drogon::Task< drogon::HttpResponsePtr > fileMetadata( const drogon::HttpRequestPtr& request );
	drogon::Task< drogon::HttpResponsePtr > file( const drogon::HttpRequestPtr& request );
	drogon::Task< drogon::HttpResponsePtr > thumbnail( const drogon::HttpRequestPtr& request );

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
	ADD_METHOD_TO(
		HydrusAPI::unsupported, "/hyapi/get_files/thumbnail_path", drogon::Get, HyAPIAuthName ); // UNSUPPORTED

	METHOD_LIST_END
};
} // namespace idhan::hyapi