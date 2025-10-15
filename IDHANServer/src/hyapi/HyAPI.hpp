//
// Created by kj16609 on 11/6/24.
//
#pragma once
#include "HyAPIAuth.hpp"
#include "HyAPIResponseEnricher.hpp"

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

#include <expected>

#include "IDHANTypes.hpp"
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

	drogon::Task< drogon::HttpResponsePtr > associateUrl( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > getUrlInfo( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > getPopups( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	// Access management
	ADD_METHOD_TO( HydrusAPI::apiVersion, "/hyapi/api_version", drogon::Get, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO(
		HydrusAPI::requestNewPermissions,
		"/hyapi/request_new_permissions",
		drogon::Get,
		HyAPIAuthName,
		RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO( HydrusAPI::sessionKey, "/hyapi/session_key", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO(
		HydrusAPI::verifyAccessKey, "/hyapi/verify_access_key", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO( HydrusAPI::getService, "/hyapi/get_service", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO( HydrusAPI::getServices, "/hyapi/get_services", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );

	// Importing and deleting files
	ADD_METHOD_TO(
		HydrusAPI::addFile, "/hyapi/add_files/add_file", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );

	// Searching and fetching files
	ADD_METHOD_TO(
		HydrusAPI::searchFiles, "/hyapi/get_files/search_files", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO(
		HydrusAPI::fileHashes, "/hyapi/get_files/file_hashes", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO(
		HydrusAPI::fileMetadata, "/hyapi/get_files/file_metadata", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO( HydrusAPI::file, "/hyapi/get_files/file", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO(
		HydrusAPI::thumbnail, "/hyapi/get_files/thumbnail", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO(
		HydrusAPI::unsupported,
		"/hyapi/get_files/file_path",
		drogon::Get,
		HyAPIAuthName,
		RESPONSE_ENRICHER_NAME ); // UNSUPPORTED

	ADD_METHOD_TO(
		HydrusAPI::unsupported,
		"/hyapi/add_urls/add_url",
		drogon::Post,
		HyAPIAuthName,
		RESPONSE_ENRICHER_NAME ); // UNSUPPORTED

	// file urls
	ADD_METHOD_TO(
		HydrusAPI::associateUrl, "/hyapi/add_urls/associate_url", drogon::Post, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO(
		HydrusAPI::getUrlInfo, "/hyapi/add_urls/get_url_info", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );

	ADD_METHOD_TO(
		HydrusAPI::unsupported,
		"/hyapi/get_files/thumbnail_path",
		drogon::Get,
		HyAPIAuthName,
		RESPONSE_ENRICHER_NAME ); // UNSUPPORTED

	ADD_METHOD_TO(
		HydrusAPI::searchTags, "/hyapi/add_tags/search_tags", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );
	ADD_METHOD_TO(
		HydrusAPI::getClientOptions,
		"/hyapi/manage_database/get_client_options",
		drogon::Get,
		HyAPIAuthName,
		RESPONSE_ENRICHER_NAME );

	ADD_METHOD_TO(
		HydrusAPI::getPopups, "/hyapi/manage_popups/get_popups", drogon::Get, HyAPIAuthName, RESPONSE_ENRICHER_NAME );

	METHOD_LIST_END
};

/**
 * @brief Converts and extracts Hydrus' `file` input from json to record ids, Sets the record parameter `file_ids` to a json array of the record ids
 * @param request
 * @param hashes
 * @param db
 * @return
 */
drogon::Task< std::expected< void, drogon::HttpResponsePtr > >
	convertQueryRecordIDs( drogon::HttpRequestPtr& request, drogon::orm::DbClientPtr db );

drogon::Task< Json::Value > getServiceList( drogon::orm::DbClientPtr db );

} // namespace idhan::hyapi