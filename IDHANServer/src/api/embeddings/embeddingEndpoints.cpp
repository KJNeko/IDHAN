#include <drogon/drogon.h>

#include <mutex>
#include <string>
#include <unordered_set>

#include "api/EmbeddingAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "embeddings/compareEmbeddings.hpp"
#include "embeddings/embeddings.hpp"
#include "embeddings/queryTerms.hpp"
#include "embeddings/searchEmbeddings.hpp"
#include "jobs/JobContext.hpp"
#include "modules/ModuleLoader.hpp"
#include "modules/RemoteModule.hpp"

namespace idhan::api
{

//! HNSW recall knob: how much of the graph a search walks. Higher is better recall and more time.
/** pgvector caps hnsw.ef_search at 1000, and an HNSW scan returns at most ef_search rows -- so this
 *  is also the ceiling on how many results the index can serve at all. */
constexpr std::size_t MAX_EF_SEARCH { 1000 };
constexpr std::size_t DEFAULT_EF_SEARCH { 100 };

//! Upper bound on results. A caller-chosen limit is an allocation of the caller's choosing.
/** Matched to MAX_EF_SEARCH deliberately. Asking for more than the index can walk does not fail --
 *  it quietly returns fewer rows than requested, which is indistinguishable from the collection
 *  simply not having more. Refusing the larger limit is the honest answer. */
constexpr std::size_t MAX_SEARCH_LIMIT { MAX_EF_SEARCH };
constexpr std::size_t DEFAULT_SEARCH_LIMIT { 200 };

//! Ceiling on terms in one compare request. Every text term is a separate call into the text tower,
//! so an unbounded list is an unbounded number of model invocations.
constexpr std::size_t MAX_COMPARE_TERMS { 64 };

//! Ceiling on records in one compare request. The panel asks for two; the endpoint is not restricted
//! to that, but it is not an excuse to score against the collection either.
constexpr std::size_t MAX_COMPARE_RECORDS { 8 };

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

		// The search panel needs this to decide whether to enable its text input. Discovering it by
		// submitting a query that comes back 400 is not an acceptable substitute.
		model[ "supports_text" ] = module != nullptr && module->supportsText();

		// reltuples, not count(*): this is shown so somebody can see what deleting a model would
		// destroy, and an estimate answers that perfectly well. An exact count would seq-scan every
		// model's table on every page load of the panel.
		//
		// -1 means the table has never been analysed, which for a freshly created one is the honest
		// answer -- reported as unknown rather than rounded to zero, since "0 embeddings" would read
		// as "nothing to lose".
		const auto stats { co_await db->execSqlCoro(
			"SELECT reltuples FROM pg_class WHERE oid = to_regclass($1)",
			std::format( "embeddings_{}", model[ "model_id" ].asInt() ) ) };

		if ( !stats.empty() && !stats[ 0 ][ "reltuples" ].isNull() )
		{
			if ( const auto estimate { stats[ 0 ][ "reltuples" ].as< double >() }; estimate >= 0.0 )
				model[ "embedding_estimate" ] = static_cast< Json::Int64 >( estimate );
		}

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

drogon::Task< drogon::HttpResponsePtr > EmbeddingAPI::search( drogon::HttpRequestPtr request )
{
	const auto json { request->getJsonObject() };
	if ( json == nullptr ) co_return createBadRequest( "Expected a JSON body" );

	if ( !( *json )[ "model_name" ].isString() ) co_return createBadRequest( "Expected a string \"model_name\"" );

	const auto model_name { ( *json )[ "model_name" ].asString() };

	const auto& terms_json { ( *json )[ "terms" ] };
	if ( !terms_json.isArray() ) co_return createBadRequest( "Expected an array \"terms\"" );
	if ( terms_json.empty() ) co_return createBadRequest( "A query needs at least one term" );

	auto parsed_terms { embeddings::parseQueryTerms( terms_json ) };
	if ( !parsed_terms ) co_return parsed_terms.error();

	auto terms { std::move( parsed_terms.value() ) };

	auto db { drogon::app().getDbClient() };

	const auto rows { co_await db->execSqlCoro(
		"SELECT model_id, model_dimensions FROM embedding_models WHERE model_name = $1", model_name ) };

	if ( rows.empty() ) co_return createNotFound( "The model \"{}\" is not registered", model_name );

	const auto model_id { rows[ 0 ][ "model_id" ].as< std::int32_t >() };
	const auto dimensions { static_cast< std::size_t >( rows[ 0 ][ "model_dimensions" ].as< std::int32_t >() ) };

	const auto limit {
		( *json )[ "limit" ].isIntegral() && ( *json )[ "limit" ].asInt64() > 0 ?
			std::min( static_cast< std::size_t >( ( *json )[ "limit" ].asInt64() ), MAX_SEARCH_LIMIT ) :
			DEFAULT_SEARCH_LIMIT
	};

	const auto requested_ef {
		( *json )[ "ef_search" ].isIntegral() && ( *json )[ "ef_search" ].asInt64() > 0 ?
			std::min( static_cast< std::size_t >( ( *json )[ "ef_search" ].asInt64() ), MAX_EF_SEARCH ) :
			DEFAULT_EF_SEARCH
	};

	// Never below the limit. An HNSW scan visits ef_search candidates and can return no more than
	// that, so ef_search < limit silently yields a short result set -- and a short set looks exactly
	// like a collection that had nothing else to give. The default of 100 against the default limit
	// of 200 would have done this on every unparameterised search.
	const auto ef_search { std::max( requested_ef, limit ) };

	// A record-only query never touches the module system at all: every vector it needs is already
	// in the table. The module is resolved only when a phrase has to be embedded.
	const auto has_text_term { std::ranges::any_of( terms, []( const auto& term ) { return term.m_is_text; } ) };

	std::shared_ptr< modules::RemoteModule > module {};

	if ( has_text_term )
	{
		module = modules::ModuleLoader::instance().getEmbedderFor( model_name );

		if ( module == nullptr )
			co_return createNotFound(
				"No loaded module provides the model \"{}\", so text terms cannot be embedded", model_name );

		if ( !module->supportsText() )
			co_return createBadRequest(
				"The model \"{}\" has no text encoder; use record references instead", model_name );
	}

	const auto started { std::chrono::steady_clock::now() };

	const auto hits {
		co_await embeddings::searchEmbeddings( module, model_id, dimensions, std::move( terms ), limit, ef_search, db )
	};

	if ( !hits ) co_return hits.error();

	// arrayValue explicitly: an empty result must serialise as [] rather than null.
	Json::Value record_ids { Json::arrayValue };
	Json::Value distances { Json::arrayValue };

	for ( const auto& hit : hits.value() )
	{
		record_ids.append( hit.m_record_id );
		distances.append( hit.m_distance );
	}

	Json::Value response {};
	response[ "record_ids" ] = std::move( record_ids );
	response[ "distances" ] = std::move( distances );
	response[ "query_ms" ] = static_cast< Json::UInt64 >(
		std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - started ).count() );

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

drogon::Task< drogon::HttpResponsePtr > EmbeddingAPI::compare( drogon::HttpRequestPtr request )
{
	const auto json { request->getJsonObject() };
	if ( json == nullptr ) co_return createBadRequest( "Expected a JSON body" );

	if ( !( *json )[ "model_name" ].isString() ) co_return createBadRequest( "Expected a string \"model_name\"" );

	const auto model_name { ( *json )[ "model_name" ].asString() };

	const auto& ids_json { ( *json )[ "record_ids" ] };
	if ( !ids_json.isArray() ) co_return createBadRequest( "Expected an array \"record_ids\"" );
	if ( ids_json.empty() ) co_return createBadRequest( "A comparison needs at least one record" );

	if ( ids_json.size() > MAX_COMPARE_RECORDS )
		co_return createBadRequest(
			"At most {} records may be compared at once; {} were given", MAX_COMPARE_RECORDS, ids_json.size() );

	std::vector< RecordID > record_ids {};
	record_ids.reserve( ids_json.size() );

	for ( const auto& entry : ids_json )
	{
		if ( !entry.isIntegral() ) co_return createBadRequest( "Every entry of \"record_ids\" must be an integer" );
		record_ids.push_back( static_cast< RecordID >( entry.asInt64() ) );
	}

	// Absent means "just tell me how far apart these records are", which is a real question and not
	// the same mistake an empty query would be in search.
	const auto& terms_json { ( *json )[ "terms" ] };

	std::vector< embeddings::QueryTerm > terms {};

	if ( !terms_json.isNull() )
	{
		if ( terms_json.size() > MAX_COMPARE_TERMS )
			co_return createBadRequest(
				"At most {} terms may be compared at once; {} were given", MAX_COMPARE_TERMS, terms_json.size() );

		auto parsed { embeddings::parseQueryTerms( terms_json ) };
		if ( !parsed ) co_return parsed.error();

		terms = std::move( parsed.value() );
	}

	auto db { drogon::app().getDbClient() };

	const auto rows { co_await db->execSqlCoro(
		"SELECT model_id, model_dimensions FROM embedding_models WHERE model_name = $1", model_name ) };

	if ( rows.empty() ) co_return createNotFound( "The model \"{}\" is not registered", model_name );

	const auto model_id { rows[ 0 ][ "model_id" ].as< std::int32_t >() };
	const auto dimensions { static_cast< std::size_t >( rows[ 0 ][ "model_dimensions" ].as< std::int32_t >() ) };

	const auto has_text_term { std::ranges::any_of( terms, []( const auto& term ) { return term.m_is_text; } ) };

	std::shared_ptr< modules::RemoteModule > module {};

	if ( has_text_term )
	{
		module = modules::ModuleLoader::instance().getEmbedderFor( model_name );

		if ( module == nullptr )
			co_return createNotFound(
				"No loaded module provides the model \"{}\", so text terms cannot be embedded", model_name );

		if ( !module->supportsText() )
			co_return createBadRequest(
				"The model \"{}\" has no text encoder; use record references instead", model_name );
	}

	const auto started { std::chrono::steady_clock::now() };

	const auto result {
		co_await embeddings::compareEmbeddings( module, model_id, dimensions, std::move( terms ), record_ids, db )
	};

	if ( !result ) co_return result.error();

	// arrayValue explicitly: an empty matrix must serialise as [] rather than null.
	Json::Value ids { Json::arrayValue };
	for ( const auto record_id : record_ids ) ids.append( record_id );

	Json::Value distances { Json::arrayValue };

	for ( const auto& row : result->m_distances )
	{
		Json::Value entry { Json::arrayValue };
		for ( const auto distance : row ) entry.append( distance );
		distances.append( std::move( entry ) );
	}

	Json::Value response {};
	response[ "record_ids" ] = std::move( ids );
	response[ "distances" ] = std::move( distances );

	if ( result->m_pair_distance.has_value() ) response[ "pair_distance" ] = result->m_pair_distance.value();

	response[ "query_ms" ] = static_cast< Json::UInt64 >(
		std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - started ).count() );

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

drogon::Task< drogon::HttpResponsePtr > EmbeddingAPI::deleteModel(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	const std::int32_t model_id )
{
	auto db { drogon::app().getDbClient() };

	const auto rows {
		co_await db->execSqlCoro( "SELECT model_name FROM embedding_models WHERE model_id = $1", model_id )
	};

	if ( rows.empty() ) co_return createNotFound( "No embedding model with id {}", model_id );

	const auto model_name { rows[ 0 ][ "model_name" ].as< std::string >() };

	// Claimed rather than merely checked. A backfill starting between a check and the delete would
	// spend the rest of its run writing rows into a table that no longer exists, one failed page at
	// a time. Holding the claim makes the two mutually exclusive.
	if ( !embeddings::tryBeginBackfill( model_id ) )
		co_return createConflict(
			"A backfill for \"{}\" is running. Wait for it to finish before deleting the model", model_name );

	try
	{
		// The AFTER DELETE trigger drops embeddings_<model_id>. Doing it there rather than here means
		// the table cannot outlive its row whatever removes it, and DDL being transactional means a
		// failure takes the drop with it.
		co_await db->execSqlCoro( "DELETE FROM embedding_models WHERE model_id = $1", model_id );
	}
	catch ( const std::exception& e )
	{
		embeddings::endBackfill( model_id );
		co_return createInternalError( "Could not delete \"{}\": {}", model_name, e.what() );
	}

	embeddings::endBackfill( model_id );

	// Deliberately loud. This destroys every vector computed for the model, which for a large
	// collection is hours of work that only a fresh backfill can replace.
	log::info( "Deleted embedding model '{}' (id {}) and every embedding it held", model_name, model_id );

	Json::Value response {};
	response[ "model_id" ] = model_id;
	response[ "model_name" ] = model_name;
	response[ "deleted" ] = true;

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

} // namespace idhan::api
