//
// Created by kj16609 on 11/8/24.
//
#pragma once
#include "IDHANTypes.hpp"
#include "drogon/HttpController.h"
#include "helpers/ResponseCallback.hpp"

namespace idhan::api
{

/**
 * @page APIDocs IDHAN API Docs
 * @note If your looking for the docs of the Hydrus API.
 * You can see a list of what is implemented @ref HydrusAPIDocs "HERE" or view the actual Hydrus docs.
 *
 * @tableofcontents
 *
 *
 *
 *
 * @subsection tag_info GET /tag/tag_id}/info
 * - Requires a valid tag_id as a part of the path.
 * - If the tag_id is not valid the body information will be blank
 * Will return:
 * - Number of files that have this tag, (Only file and tag domains you can see)
 */

/**
 */
class IDHANApi : public drogon::HttpController< IDHANApi >
{
	void api( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
	void apiDocs( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

	void version( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

	drogon::Task< drogon::HttpResponsePtr > info( drogon::HttpRequestPtr request, TagID tag_id );

	drogon::Task< drogon::HttpResponsePtr > search( drogon::HttpRequestPtr request, std::string tag_text );

	drogon::Task< drogon::HttpResponsePtr > autocomplete( drogon::HttpRequestPtr request, std::string tag_id );

	drogon::Task< drogon::HttpResponsePtr > createSingleTag( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > createBatchedTag( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > createTagRouter( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( IDHANApi::api, "/api" );
	ADD_METHOD_VIA_REGEX( IDHANApi::apiDocs, "/api.*.yaml" );

	ADD_METHOD_TO( IDHANApi::version, "/version" );

	ADD_METHOD_TO( IDHANApi::info, "/tag/{tag_id}/info" );
	ADD_METHOD_TO( IDHANApi::info, "/tag/info?tag_id={1}" );
	ADD_METHOD_TO( IDHANApi::info, "/tag/info?tag_ids={1}" );

	ADD_METHOD_TO( IDHANApi::search, "/tag/search?tag={1}" );
	// ADD_METHOD_TO( IDHANApi::autocomplete, "/tag/autocomplete?tag={1}" );

	ADD_METHOD_TO( IDHANApi::createTagRouter, "/tag/create" );

	METHOD_LIST_END
};

} // namespace idhan::api
