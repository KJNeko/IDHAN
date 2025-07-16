//
// Created by kj16609 on 3/22/25.
//
#pragma once

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
#include <drogon/HttpController.h>
#pragma GCC diagnostic pop

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