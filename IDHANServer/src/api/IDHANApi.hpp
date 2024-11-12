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

	void tagInfo( const drogon::HttpRequestPtr& request, ResponseFunction&& callback, TagID tag_id );

	drogon::Task< drogon::HttpResponsePtr > createTag( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( IDHANApi::api, "/api" );
	ADD_METHOD_VIA_REGEX( IDHANApi::apiDocs, "/api.*.yaml" );

	ADD_METHOD_TO( IDHANApi::version, "/version" );

	ADD_METHOD_TO( IDHANApi::tagInfo, "/tag/{tag_id}/info" );
	ADD_METHOD_TO( IDHANApi::tagInfo, "/tag/info?tag_id={1}" );
	ADD_METHOD_TO( IDHANApi::tagInfo, "/tag/info?tag_ids={1}" );

	ADD_METHOD_TO( IDHANApi::createTag, "/tag/create" );

	METHOD_LIST_END
};

} // namespace idhan::api
