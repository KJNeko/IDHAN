//
// Created by kj16609 on 11/18/24.
//
#pragma once
#include "IDHANTypes.hpp"
#include "drogon/HttpController.h"

namespace idhan::api
{

class IDHANTagAPI : public drogon::HttpController< IDHANTagAPI >
{
	drogon::Task< drogon::HttpResponsePtr > info( drogon::HttpRequestPtr request, TagID tag_id );

	drogon::Task< drogon::HttpResponsePtr > search( drogon::HttpRequestPtr request, std::string tag_text );

	drogon::Task< drogon::HttpResponsePtr > autocomplete( drogon::HttpRequestPtr request, std::string tag_id );

	drogon::Task< drogon::HttpResponsePtr > createSingleTag( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > createBatchedTag( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > createTagRouter( drogon::HttpRequestPtr request );

  public:

	IDHANTagAPI() = default;

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( IDHANTagAPI::info, "/tag/{tag_id}/info" );
	ADD_METHOD_TO( IDHANTagAPI::info, "/tag/info?tag_id={1}" );
	ADD_METHOD_TO( IDHANTagAPI::info, "/tag/info?tag_ids={1}" );

	ADD_METHOD_TO( IDHANTagAPI::search, "/tag/search?tag={1}" );
	// ADD_METHOD_TO( IDHANTagAPI::autocomplete, "/tag/autocomplete?tag={1}" );

	ADD_METHOD_TO( IDHANTagAPI::createTagRouter, "/tag/create" );

	METHOD_LIST_END
};

} // namespace idhan::api
