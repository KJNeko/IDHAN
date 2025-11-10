//
// Created by kj16609 on 11/5/25.
//
#pragma once
#include "drogon/HttpController.h"

namespace idhan::api
{

class FileRelationshipsAPI : public drogon::HttpController< FileRelationshipsAPI >
{
	drogon::Task< drogon::HttpResponsePtr > setBetterDuplicate( drogon::HttpRequestPtr request );
	drogon::Task< drogon::HttpResponsePtr > addAlternative( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( FileRelationshipsAPI::setBetterDuplicate, "/relationships/duplicates/add" );

	ADD_METHOD_TO( FileRelationshipsAPI::addAlternative, "/relationships/alternatives/add" );

	METHOD_LIST_END
};

} // namespace idhan::api