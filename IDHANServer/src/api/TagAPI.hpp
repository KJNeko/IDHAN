//
// Created by kj16609 on 11/18/24.
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

namespace idhan::api
{

class TagAPI : public drogon::HttpController< TagAPI >
{
	drogon::Task< drogon::HttpResponsePtr > getTagInfo( drogon::HttpRequestPtr request, TagID tag_id );

	drogon::Task< drogon::HttpResponsePtr > search( drogon::HttpRequestPtr request, std::string tag_text );

	drogon::Task< drogon::HttpResponsePtr > autocomplete( drogon::HttpRequestPtr request, std::string tag_text );

	drogon::Task< drogon::HttpResponsePtr > createBatchedTag( drogon::HttpRequestPtr request );

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

	ADD_METHOD_TO( TagAPI::getTagInfo, "/tags/{tag_id}/info" );
	ADD_METHOD_TO( TagAPI::getTagInfo, "/tags/info?tag_id={1}" );
	ADD_METHOD_TO( TagAPI::getTagInfo, "/tags/info?tag_ids={1}" );
	ADD_METHOD_TO( TagAPI::getTagRelationships, "/tags/{domain_id}/{tag_id}/relationships" );

	ADD_METHOD_TO( TagAPI::search, "/tags/search?tag={1}" );
	ADD_METHOD_TO( TagAPI::autocomplete, "/tags/autocomplete?tag={1}" );

	ADD_METHOD_TO( TagAPI::createBatchedTag, "/tags/create" );

	ADD_METHOD_TO( TagAPI::createTagDomain, "/tags/domain/create" );
	ADD_METHOD_TO( TagAPI::getTagDomains, "/tags/domain/list" );
	ADD_METHOD_TO( TagAPI::getTagDomainInfo, "/tags/domain/{domain_id}/info" );
	ADD_METHOD_TO( TagAPI::deleteTagDomain, "/tags/domain/{domain_id}/delete" );

	ADD_METHOD_TO( TagAPI::createTagParents, "/tags/parents/create" );
	ADD_METHOD_TO( TagAPI::createTagAliases, "/tags/alias/create" );

	METHOD_LIST_END
};

drogon::Task< Json::Value >
	getSimilarTags( std::string search_value, drogon::orm::DbClientPtr db, std::size_t limit = 10 );

} // namespace idhan::api
