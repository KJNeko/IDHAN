//
// Created by kj16609 on 11/6/24.
//
#pragma once
#include "HyAPIAuth.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wredundant-tags"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wnoexcept"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#pragma GCC diagnostic ignored "-Wshadow"
#include "drogon/HttpController.h"
#pragma GCC diagnostic pop

#include "api/helpers/ResponseCallback.hpp"

namespace idhan::hyapi
{
class HydrusAPI : public drogon::HttpController< HydrusAPI >
{
	drogon::Task< drogon::HttpResponsePtr > unsupported( drogon::HttpRequestPtr request );

	// Access management (access)
	drogon::Task< drogon::HttpResponsePtr > apiVersion( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > requestNewPermissions( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > sessionKey( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > verifyAccessKey( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > getService( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > getServices( drogon::HttpRequestPtr request );

	// Importing and deleting files (import)
	drogon::Task< drogon::HttpResponsePtr > addFile( drogon::HttpRequestPtr request );

	// Searching and Fetching files (search)
	drogon::Task< drogon::HttpResponsePtr > searchFiles( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > fileHashes( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > fileMetadata( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > file( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > thumbnail( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > searchTags( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > getClientOptions( drogon::HttpRequestPtr request );

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
	ADD_METHOD_TO( HydrusAPI::addFile, "/hyapi/add_files/add_file", drogon::Get, HyAPIAuthName );

	// Searching and fetching files
	ADD_METHOD_TO( HydrusAPI::searchFiles, "/hyapi/get_files/search_files", drogon::Get, HyAPIAuthName );
	ADD_METHOD_TO( HydrusAPI::fileHashes, "/hyapi/get_files/file_hashes", drogon::Get, HyAPIAuthName );
	ADD_METHOD_TO( HydrusAPI::fileMetadata, "/hyapi/get_files/file_metadata", drogon::Get, HyAPIAuthName );
	ADD_METHOD_TO( HydrusAPI::file, "/hyapi/get_files/file", drogon::Get, HyAPIAuthName );
	ADD_METHOD_TO( HydrusAPI::thumbnail, "/hyapi/get_files/thumbnail", drogon::Get, HyAPIAuthName );
	ADD_METHOD_TO( HydrusAPI::unsupported, "/hyapi/get_files/file_path", drogon::Get, HyAPIAuthName ); // UNSUPPORTED
	ADD_METHOD_TO(
		HydrusAPI::unsupported, "/hyapi/get_files/thumbnail_path", drogon::Get, HyAPIAuthName ); // UNSUPPORTED

	ADD_METHOD_TO( HydrusAPI::searchTags, "/hyapi/add_tags/search_tags", drogon::Get, HyAPIAuthName );
	ADD_METHOD_TO(
		HydrusAPI::getClientOptions, "/hyapi/manage_database/get_client_options", drogon::Get, HyAPIAuthName );
	METHOD_LIST_END
};
} // namespace idhan::hyapi