#pragma once

#include <drogon/HttpController.h>

#include "APIAuth.hpp"

namespace idhan::api
{

class SearchAPI : public drogon::HttpController< SearchAPI >
{
  public:

	//! Legacy tag-id-only search. Returns a bare array of record ids. Kept for back-compat.
	static drogon::Task< drogon::HttpResponsePtr > search( drogon::HttpRequestPtr );

	//! Full search from a JSON body: tag text with negation and system predicates, sort, and
	//! limit/offset. Returns { record_ids, count, truncated, query_ms }.
	static drogon::Task< drogon::HttpResponsePtr > searchPost( drogon::HttpRequestPtr );

	METHOD_LIST_BEGIN

	ADD_METHOD_TO( SearchAPI::search, "/search", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( SearchAPI::searchPost, "/search", drogon::Post, IDHANAPIAuthName );

	METHOD_LIST_END
};

} // namespace idhan::api
