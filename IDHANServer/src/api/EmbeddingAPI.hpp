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

//! Endpoints for image embeddings: what models exist, filling in the vectors, and searching them.
/** Search is standalone top-K over one model's vectors, ordered by cosine distance. Composing it
 *  with tag predicates is a follow-up rather than an omission: pre-filtering and post-filtering
 *  trade recall against speed in ways that depend on collection size and on how selective real
 *  queries turn out to be, and there is no data to choose between them on yet. */
class EmbeddingAPI : public drogon::HttpController< EmbeddingAPI >
{
	using ResponseTask = drogon::Task< drogon::HttpResponsePtr >;

	ResponseTask listModels( drogon::HttpRequestPtr request );

	ResponseTask generate( drogon::HttpRequestPtr request );

	ResponseTask search( drogon::HttpRequestPtr request );

  public:

	METHOD_LIST_BEGIN
	ADD_METHOD_TO( EmbeddingAPI::listModels, "/embeddings/models", drogon::Get, IDHANAPIAuthName );
	ADD_METHOD_TO( EmbeddingAPI::generate, "/embeddings/generate", drogon::Post, IDHANAPIAuthName );
	ADD_METHOD_TO( EmbeddingAPI::search, "/embeddings/search", drogon::Post, IDHANAPIAuthName );
	METHOD_LIST_END
};

} // namespace idhan::api
