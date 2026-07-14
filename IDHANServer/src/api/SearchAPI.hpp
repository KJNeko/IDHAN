//
// Created by kj16609 on 3/22/25.
//
#pragma once

#include <drogon/HttpController.h>

#include "APIAuth.hpp"

namespace idhan::api
{

//! Endpoint for searching records by tag and system predicates (backed by SearchBuilder).
class SearchAPI : public drogon::HttpController< SearchAPI >
{
  public:

	static drogon::Task< drogon::HttpResponsePtr > search( drogon::HttpRequestPtr );

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( SearchAPI::search, "/search", drogon::Get, IDHANAPIAuthName );

	METHOD_LIST_END
};

} // namespace idhan::api
