//
// Created by kj16609 on 11/8/24.
//
#pragma once
#include "IDHANTypes.hpp"

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
class InfoAPI : public drogon::HttpController< InfoAPI >
{
	void api( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );
	void apiDocs( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

	void version( const drogon::HttpRequestPtr& request, ResponseFunction&& callback );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( InfoAPI::api, "/api" );
	ADD_METHOD_VIA_REGEX( InfoAPI::apiDocs, "/api.*.yaml" );

	ADD_METHOD_TO( InfoAPI::version, "/version" );

	METHOD_LIST_END
};

} // namespace idhan::api
