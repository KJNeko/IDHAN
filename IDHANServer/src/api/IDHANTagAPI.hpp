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
	drogon::Task< drogon::HttpResponsePtr > getTagInfo( drogon::HttpRequestPtr request, TagID tag_id );

	drogon::Task< drogon::HttpResponsePtr > search( drogon::HttpRequestPtr request, std::string tag_text );

	drogon::Task< drogon::HttpResponsePtr > autocomplete( drogon::HttpRequestPtr request, std::string tag_id );

	drogon::Task< drogon::HttpResponsePtr > createSingleTag( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > createBatchedTag( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > createTagRouter( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > createTagDomain( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > getTagDomains( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > getTagDomainInfo( drogon::HttpRequestPtr request, TagDomainID domain_id );
	drogon::Task< drogon::HttpResponsePtr > deleteTagDomain( drogon::HttpRequestPtr request, TagDomainID domain_id );

	drogon::Task< drogon::HttpResponsePtr > createTagParents( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > createTagAliases( drogon::HttpRequestPtr request );

  public:

	IDHANTagAPI() = default;

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( IDHANTagAPI::getTagInfo, "/tag/{tag_id}/info" );
	ADD_METHOD_TO( IDHANTagAPI::getTagInfo, "/tag/info?tag_id={1}" );
	ADD_METHOD_TO( IDHANTagAPI::getTagInfo, "/tag/info?tag_ids={1}" );

	ADD_METHOD_TO( IDHANTagAPI::search, "/tag/search?tag={1}" );
	// ADD_METHOD_TO( IDHANTagAPI::autocomplete, "/tag/autocomplete?tag={1}" );

	ADD_METHOD_TO( IDHANTagAPI::createTagRouter, "/tag/create" );

	ADD_METHOD_TO( IDHANTagAPI::createTagDomain, "/tag/domain/create" );
	ADD_METHOD_TO( IDHANTagAPI::getTagDomains, "/tag/domain/list" );
	ADD_METHOD_TO( IDHANTagAPI::getTagDomainInfo, "/tag/domain/{domain_id}/info" );
	ADD_METHOD_TO( IDHANTagAPI::deleteTagDomain, "/tag/domain/{domain_id}/delete" );

	ADD_METHOD_TO( IDHANTagAPI::createTagParents, "/tag/parents/create" );
	ADD_METHOD_TO( IDHANTagAPI::createTagAliases, "/tag/alias/create" );

	METHOD_LIST_END
};

} // namespace idhan::api
