//
// Created by kj16609 on 3/22/25.
//
#pragma once

#include <drogon/HttpController.h>

#include "APIAuth.hpp"

namespace idhan::api
{

class SearchAPI : public drogon::HttpController< SearchAPI >
{
  public:

	static drogon::Task< drogon::HttpResponsePtr > search( drogon::HttpRequestPtr );

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( SearchAPI::search, "/search", IDHANAPIAuthName );

	METHOD_LIST_END
};

} // namespace idhan::api
