//
// Created by kj16609 on 11/18/24.
//
#pragma once

#include <algorithm>
#include <string>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "drogon/HttpController.h"

namespace idhan::api
{
class TagAPI : public drogon::HttpController< TagAPI >
{
	drogon::Task< drogon::HttpResponsePtr > getTagInfo( drogon::HttpRequestPtr request, TagID tag_id );

	drogon::Task< drogon::HttpResponsePtr > search( drogon::HttpRequestPtr request, std::string tag_text );

	drogon::Task< drogon::HttpResponsePtr > autocomplete( drogon::HttpRequestPtr request, std::string tag_text );

	drogon::Task< drogon::HttpResponsePtr > createTagsFromRequest( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > createTagDomain( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > getTagDomains( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr >
		getTagDomainInfo( drogon::HttpRequestPtr request, TagDomainID tag_domain_id );

	drogon::Task< drogon::HttpResponsePtr >
		deleteTagDomain( drogon::HttpRequestPtr request, TagDomainID tag_domain_id );

	drogon::Task< drogon::HttpResponsePtr > createTagParents( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr > createTagAliases( drogon::HttpRequestPtr request );

	drogon::Task< drogon::HttpResponsePtr >
		getTagRelationships( drogon::HttpRequestPtr request, TagDomainID tag_domain_id, TagID tag_id );

  public:

	TagAPI() = default;

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( TagAPI::getTagInfo, "/tags/{tag_id}/info", drogon::Get );
	ADD_METHOD_TO( TagAPI::getTagInfo, "/tags/info?tag_id={1}", drogon::Get );
	ADD_METHOD_TO( TagAPI::getTagInfo, "/tags/info?tag_ids={1}", drogon::Get );
	ADD_METHOD_TO( TagAPI::getTagRelationships, "/tags/{domain_id}/{tag_id}/relationships", drogon::Get );

	ADD_METHOD_TO( TagAPI::search, "/tags/search?tag={1}", drogon::Get );
	ADD_METHOD_TO( TagAPI::autocomplete, "/tags/autocomplete?tag={1}", drogon::Get );

	ADD_METHOD_TO( TagAPI::createTagsFromRequest, "/tags/create", drogon::Post );

	ADD_METHOD_TO( TagAPI::createTagDomain, "/tags/domain/create", drogon::Post );
	ADD_METHOD_TO( TagAPI::getTagDomains, "/tags/domain/list", drogon::Get );
	ADD_METHOD_TO( TagAPI::getTagDomainInfo, "/tags/domain/{tag_domain_id}/info", drogon::Get );
	ADD_METHOD_TO( TagAPI::deleteTagDomain, "/tags/domain/{tag_domain_id}/delete", drogon::Delete );

	ADD_METHOD_TO( TagAPI::createTagParents, "/tags/parents/create", drogon::Post );
	ADD_METHOD_TO( TagAPI::createTagAliases, "/tags/alias/create", drogon::Post );

	METHOD_LIST_END
};

drogon::Task< Json::Value > getSimilarTags( std::string search_value, DbClientPtr db, std::size_t limit = 10 );
} // namespace idhan::api
