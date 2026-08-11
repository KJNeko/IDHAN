//
// Created by kj16609 on 8/10/26.
//

#include <drogon/drogon.h>

#include <mutex>
#include <string>
#include <unordered_set>

#include "api/EmbeddingAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "embeddings/embeddings.hpp"
#include "jobs/JobContext.hpp"
#include "modules/ModuleLoader.hpp"
#include "modules/RemoteModule.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > EmbeddingAPI::listModels( [[maybe_unused]] drogon::HttpRequestPtr request )
{
	auto db { drogon::app().getDbClient() };

	// arrayValue explicitly: a default-constructed Json::Value is null until something is appended,
	// so no registered models would serialise as null rather than [].
	Json::Value models { Json::arrayValue };

	const auto rows { co_await db->execSqlCoro(
		"SELECT model_id, model_name, model_dimensions FROM embedding_models ORDER BY model_id" ) };

	for ( const auto& row : rows )
	{
		Json::Value model {};
		model[ "model_id" ] = row[ "model_id" ].as< std::int32_t >();
		model[ "model_name" ] = row[ "model_name" ].as< std::string >();
		model[ "dimensions" ] = row[ "model_dimensions" ].as< std::int32_t >();

		// A model can be registered in the database while no loaded library currently provides it --
		// the module was removed, or its .so failed to load. Saying so beats letting a backfill be
		// queued against a model nothing can serve.
		const auto module { modules::ModuleLoader::instance().getEmbedderFor( model[ "model_name" ].asString() ) };
		model[ "available" ] = module != nullptr;

		models.append( std::move( model ) );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( models );
}

drogon::Task< drogon::HttpResponsePtr > EmbeddingAPI::generate( drogon::HttpRequestPtr request )
{
	const auto json { request->getJsonObject() };
	if ( json == nullptr ) co_return createBadRequest( "Expected a JSON body" );

	// Checked before as*(): jsoncpp throws on the wrong type, which would surface as a 500 where a
	// 400 belongs.
	if ( !( *json )[ "model_name" ].isString() ) co_return createBadRequest( "Expected a string \"model_name\"" );

	const auto model_name { ( *json )[ "model_name" ].asString() };

	const auto module { modules::ModuleLoader::instance().getEmbedderFor( model_name ) };
	if ( module == nullptr ) co_return createNotFound( "No loaded module provides the model \"{}\"", model_name );

	auto db { drogon::app().getDbClient() };

	const auto rows { co_await db->execSqlCoro(
		"SELECT model_id, model_dimensions FROM embedding_models WHERE model_name = $1", model_name ) };

	if ( rows.empty() ) co_return createNotFound( "The model \"{}\" is not registered in the database", model_name );

	const auto model_id { rows[ 0 ][ "model_id" ].as< std::int32_t >() };

	if ( const auto stored { rows[ 0 ][ "model_dimensions" ].as< std::int32_t >() };
	     stored != static_cast< std::int32_t >( module->dimensions() ) )
		co_return createConflict(
			"Model \"{}\" is registered with {} dimensions but the loaded module produces {}",
			model_name,
			stored,
			module->dimensions() );

	// Claimed here so a duplicate request is refused synchronously; the job releases it when it ends.
	if ( !embeddings::tryBeginBackfill( model_id ) )
		co_return createConflict( "A backfill for \"{}\" is already running", model_name );

	auto job_ctx { queueJob(
		embeddings::backfillJob( model_id, model_name ), std::format( "Embedding backfill for {}", model_name ) ) };

	Json::Value response {};
	response[ "job_id" ] = job_ctx->id();
	response[ "model_id" ] = model_id;
	response[ "status" ] = "dispatched";

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api
