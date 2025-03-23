//
// Created by kj16609 on 3/22/25.
//
#pragma once

#include <drogon/HttpController.h>

namespace idhan::api
{

class IDHANSearchAPI : public drogon::HttpController< IDHANSearchAPI >
{
  public:

	static drogon::Task< drogon::HttpResponsePtr > search( drogon::HttpRequestPtr );

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( IDHANSearchAPI::search, "/search" );

	METHOD_LIST_END
};

} // namespace idhan::api