//
// Created by kj16609 on 8/10/26.
//
#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

#include "APIAuth.hpp"
#include "IDHANTypes.hpp"

namespace idhan::api
{

//! Endpoints for image embeddings: what models exist, and filling in the vectors for one.
/** Generation only. Searching over the vectors is deliberately absent -- it wants an HNSW index,
 *  which is cheaper to build once a backfill has finished than to maintain while one runs. */
class EmbeddingAPI : public drogon::HttpController< EmbeddingAPI >
{
	using ResponseTask = drogon::Task< drogon::HttpResponsePtr >;

	ResponseTask listModels( drogon::HttpRequestPtr request );

	ResponseTask generate( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( EmbeddingAPI::listModels, "/embeddings/models", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( EmbeddingAPI::generate, "/embeddings/generate", drogon::Post, IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
