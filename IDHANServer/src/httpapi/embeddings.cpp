#include "embeddings.hpp"

#include <folly/io/IOBuf.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <simdjson.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "apiAuth.hpp"
#include "embeddings/QueryTerm.hpp"
#include "embeddings/embeddings.hpp"
#include "embeddings/queryVector.hpp"
#include "embeddings/resolveTerms.hpp"
#include "idValidation.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "jobs/JobContext.hpp"
#include "jsonBody.hpp"
#include "logging/log.hpp"
#include "modules/ModuleLoader.hpp"
#include "modules/RemoteModule.hpp"

namespace idhan::httpapi
{

constexpr std::size_t max_ef_search { 1000 };
constexpr std::size_t default_ef_search { 100 };
constexpr std::size_t max_search_limit { max_ef_search };
constexpr std::size_t default_search_limit { 200 };
constexpr std::size_t max_compare_terms { 64 };
constexpr std::size_t max_compare_records { 8 };

[[nodiscard]] static std::expected< embeddings::QueryTerm, http::Response > parseQueryTerm(
	const simdjson::dom::element entry )
{
	if ( !entry.is_object() ) return std::unexpected( http::Response::error( 400, "Every term must be an object" ) );

	std::string_view type {};
	if ( entry[ "type" ].get_string().get( type ) != simdjson::SUCCESS )
		return std::unexpected( http::Response::error( 400, "Every term needs a string \"type\"" ) );

	embeddings::QueryTerm term {};

	double weight {};
	term.m_weight =
		entry[ "weight" ].get_double().get( weight ) == simdjson::SUCCESS ? static_cast< float >( weight ) : 1.0f;

	if ( type == "text" )
	{
		std::string_view text {};
		if ( entry[ "text" ].get_string().get( text ) != simdjson::SUCCESS )
			return std::unexpected( http::Response::error( 400, "A text term needs a string \"text\"" ) );

		if ( text.empty() ) return std::unexpected( http::Response::error( 400, "A text term cannot be empty" ) );

		term.m_is_text = true;
		term.m_text = std::string { text };
	}
	else if ( type == "record" )
	{
		std::int64_t record_id {};
		if ( entry[ "record_id" ].get_int64().get( record_id ) != simdjson::SUCCESS )
			return std::unexpected( http::Response::error( 400, "A record term needs an integral \"record_id\"" ) );

		term.m_is_text = false;
		auto id { positiveId< RecordID >( record_id, "record_id" ) };
		if ( !id ) return std::unexpected( std::move( id.error() ) );

		term.m_record_id = *id;
	}
	else
	{
		return std::unexpected(
			http::Response::error(
				400, std::format( "Unknown term type \"{}\"; expected \"text\" or \"record\"", type ) ) );
	}

	return term;
}

[[nodiscard]] static std::expected< std::vector< embeddings::QueryTerm >, http::Response > parseQueryTerms(
	const simdjson::dom::element terms_json )
{
	if ( !terms_json.is_array() ) return std::unexpected( http::Response::error( 400, "Expected an array \"terms\"" ) );

	simdjson::dom::array array {};
	if ( terms_json.get_array().get( array ) != simdjson::SUCCESS )
		return std::unexpected(
			http::Response::error( 500, "Unexpected: array conversion failed after is_array() check" ) );

	std::vector< embeddings::QueryTerm > terms {};
	terms.reserve( array.size() );

	for ( const simdjson::dom::element entry : array )
	{
		auto term { parseQueryTerm( entry ) };
		if ( !term ) return std::unexpected( std::move( term.error() ) );
		terms.emplace_back( std::move( *term ) );
	}

	return terms;
}

[[nodiscard]] static http::HTTPTask< std::expected< std::vector< embeddings::WeightedVector >, http::Response > >
	resolveQueryTerms(
		db::DBContext& db,
		std::shared_ptr< modules::RemoteModule > module,
		const std::int32_t model_id,
		std::vector< embeddings::QueryTerm > terms )
{
	const auto table { std::format( "embeddings_{}", model_id ) };

	std::vector< RecordID > wanted {};
	for ( const auto& term : terms )
		if ( !term.m_is_text ) wanted.push_back( term.m_record_id );

	std::unordered_map< RecordID, std::vector< float > > vectors {};

	if ( !wanted.empty() )
	{
		db::DBBindings bindings {};
		bindings.bind( wanted );
		const auto rows {
			co_await db
				.query(
					std::format(
						"SELECT record_id, embedding::text AS embedding FROM {} WHERE record_id = ANY($1)", table ),
					std::move( bindings ) )
				.rows< RecordID, std::string >()
		};

		for ( const auto& [ record_id, embedding_text ] : rows )
			vectors.emplace( record_id, embeddings::parseHalfvecLiteral( embedding_text ) );
	}

	std::vector< embeddings::WeightedVector > resolved {};
	resolved.reserve( terms.size() );

	for ( const auto& term : terms )
	{
		if ( term.m_is_text )
		{
			if ( !module )
				co_return std::unexpected(
					http::Response::error( 500, "A text term reached the resolver with no module to embed it" ) );

			const auto embedded { co_await module->embedText( term.m_text ) };

			if ( !embedded )
				co_return std::unexpected(
					http::Response::error(
						400, std::format( "Could not embed \"{}\": {}", term.m_text, embedded.error() ) ) );

			resolved.emplace_back(
				embeddings::WeightedVector { .m_vector = embedded->m_vector, .m_weight = term.m_weight } );
			continue;
		}

		const auto found { vectors.find( term.m_record_id ) };
		if ( found == vectors.end() )
			co_return std::unexpected(
				http::Response::error(
					404,
					std::format(
						"Record {} has no embedding for this model. Run a backfill first, or remove it from the query",
						term.m_record_id ) ) );

		resolved.emplace_back( embeddings::WeightedVector { .m_vector = found->second, .m_weight = term.m_weight } );
	}

	co_return resolved;
}

http::HTTPTask< http::JsonResult< std::vector< EmbeddingModelResponse > > > listModels(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto rows {
		co_await db.query( "SELECT model_id, model_name, model_dimensions FROM embedding_models ORDER BY model_id" )
			.rows< std::int32_t, std::string, std::int32_t >()
	};

	std::vector< EmbeddingModelResponse > response;
	response.reserve( rows.size() );
	for ( const auto& [ model_id, model_name, dimensions ] : rows )
	{
		const auto module { modules::ModuleLoader::instance().getEmbedderFor( model_name ) };
		EmbeddingModelResponse model {
			model_id, model_name, dimensions, module != nullptr, module != nullptr && module->supportsText()
		};

		db::DBBindings bindings {};
		bindings.bind( std::format( "embeddings_{}", model_id ) );
		const auto stats {
			co_await db.query( "SELECT reltuples FROM pg_class WHERE oid = to_regclass($1)", std::move( bindings ) )
				.rows< std::optional< float > >()
		};

		if ( !stats.empty() )
		{
			const auto estimate { std::get< 0 >( stats.row( 0 ) ) };
			if ( estimate && *estimate >= 0.0f )
			{
				model.embedding_estimate.emplace( static_cast< std::int64_t >( *estimate ) );
			}
		}

		response.push_back( std::move( model ) );
	}

	co_return http::JsonResponse< std::vector< EmbeddingModelResponse > > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< DeletedEmbeddingModelResponse > > deleteModel(
	db::DBContext& db,
	http::RequestContext context )
{
	const auto model_id_e { context.parameters().scalar< std::int32_t >( "model_id" ) };
	if ( !model_id_e ) co_return http::Response::error( 400, model_id_e.error().message );
	const auto model_id { *model_id_e };

	db::DBBindings find_bindings {};
	find_bindings.bind( model_id );
	const auto rows {
		co_await db.query( "SELECT model_name FROM embedding_models WHERE model_id = $1", std::move( find_bindings ) )
			.rows< std::string >()
	};

	if ( rows.empty() )
		co_return http::Response::error( 404, std::format( "No embedding model with id {}", model_id ) );

	const auto model_name { std::get< 0 >( rows.row( 0 ) ) };

	if ( !embeddings::tryBeginBackfill( model_id ) )
		co_return http::Response::error(
			409,
			std::format(
				"A backfill for \"{}\" is running. Wait for it to finish before deleting the model", model_name ) );

	try
	{
		db::DBBindings delete_bindings {};
		delete_bindings.bind( model_id );
		co_await db.query( "DELETE FROM embedding_models WHERE model_id = $1", std::move( delete_bindings ) ).result();
	}
	catch ( const std::exception& e )
	{
		embeddings::endBackfill( model_id );
		co_return http::Response::error( 500, std::format( "Could not delete \"{}\": {}", model_name, e.what() ) );
	}

	embeddings::endBackfill( model_id );

	log::info( "Deleted embedding model '{}' (id {}) and every embedding it held", model_name, model_id );

	co_return http::JsonResponse< DeletedEmbeddingModelResponse > { { model_id, model_name, true } };
}

http::HTTPTask< http::JsonResult< GenerateEmbeddingsResponse > > generateEmbeddings(
	db::DBContext& db,
	http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::element root {};
	try
	{
		root = parser.parse( *body_e );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	if ( !root.is_object() ) co_return http::Response::error( 400, "Expected a JSON body" );

	std::string_view model_name_sv {};
	if ( root[ "model_name" ].get_string().get( model_name_sv ) != simdjson::SUCCESS )
		co_return http::Response::error( 400, "Expected a string \"model_name\"" );
	const std::string model_name { model_name_sv };

	const auto module { modules::ModuleLoader::instance().getEmbedderFor( model_name ) };
	if ( !module )
		co_return http::Response::error( 404, std::format( "No loaded module provides the model \"{}\"", model_name ) );

	const auto model { co_await embeddings::findEmbeddingModel( model_name, db ) };
	if ( !model )
		co_return http::Response::error(
			404, std::format( "The model \"{}\" is not registered in the database", model_name ) );

	const auto model_id { model->id };

	if ( model->dimensions != static_cast< std::int32_t >( module->dimensions() ) )
		co_return http::Response::error(
			409,
			std::format(
				"Model \"{}\" is registered with {} dimensions but the loaded module produces {}",
				model_name,
				model->dimensions,
				module->dimensions() ) );

	if ( !embeddings::tryBeginBackfill( model_id ) )
		co_return http::Response::error( 409, std::format( "A backfill for \"{}\" is already running", model_name ) );

	auto job_ctx { queueJob(
		embeddings::backfillJob( model_id, model_name, db ), std::format( "Embedding backfill for {}", model_name ) ) };

	co_return http::JsonResponse< GenerateEmbeddingsResponse > { { job_ctx->id(), model_id, "dispatched" } };
}

http::HTTPTask< http::JsonResult< SearchEmbeddingsResponse > > searchEmbeddings(
	db::DBContext& db,
	http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::element root {};
	try
	{
		root = parser.parse( *body_e );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	if ( !root.is_object() ) co_return http::Response::error( 400, "Expected a JSON body" );

	std::string_view model_name_sv {};
	if ( root[ "model_name" ].get_string().get( model_name_sv ) != simdjson::SUCCESS )
		co_return http::Response::error( 400, "Expected a string \"model_name\"" );
	const std::string model_name { model_name_sv };

	simdjson::dom::element terms_json {};
	if ( root[ "terms" ].get( terms_json ) != simdjson::SUCCESS )
		co_return http::Response::error( 400, "Expected an array \"terms\"" );

	auto parsed_terms { parseQueryTerms( terms_json ) };
	if ( !parsed_terms ) co_return std::move( parsed_terms.error() );
	auto terms { std::move( *parsed_terms ) };

	if ( terms.empty() ) co_return http::Response::error( 400, "A query needs at least one term" );

	const auto model { co_await embeddings::findEmbeddingModel( model_name, db ) };
	if ( !model )
		co_return http::Response::error( 404, std::format( "The model \"{}\" is not registered", model_name ) );

	const auto model_id { model->id };
	const auto dimensions { static_cast< std::size_t >( model->dimensions ) };

	std::int64_t limit_value {};
	const std::size_t limit {
		root[ "limit" ].get_int64().get( limit_value ) == simdjson::SUCCESS && limit_value > 0 ?
			std::min( static_cast< std::size_t >( limit_value ), max_search_limit ) :
			default_search_limit
	};

	std::int64_t ef_value {};
	const std::size_t requested_ef {
		root[ "ef_search" ].get_int64().get( ef_value ) == simdjson::SUCCESS && ef_value > 0 ?
			std::min( static_cast< std::size_t >( ef_value ), max_ef_search ) :
			default_ef_search
	};

	const std::size_t ef_search { std::max( requested_ef, limit ) };

	const bool has_text_term { std::ranges::any_of( terms, []( const auto& term ) { return term.m_is_text; } ) };

	std::shared_ptr< modules::RemoteModule > module {};

	if ( has_text_term )
	{
		module = modules::ModuleLoader::instance().getEmbedderFor( model_name );

		if ( !module )
			co_return http::Response::error(
				404,
				std::format(
					"No loaded module provides the model \"{}\", so text terms cannot be embedded", model_name ) );

		if ( !module->supportsText() )
			co_return http::Response::error(
				400, std::format( "The model \"{}\" has no text encoder; use record references instead", model_name ) );
	}

	const auto started { std::chrono::steady_clock::now() };

	auto resolved_e { co_await resolveQueryTerms( db, std::move( module ), model_id, std::move( terms ) ) };
	if ( !resolved_e ) co_return std::move( resolved_e.error() );

	const auto query_vector { embeddings::assembleQueryVector( *resolved_e, dimensions ) };
	if ( !query_vector ) co_return http::Response::error( 400, query_vector.error() );

	auto transaction { co_await db.transaction() };

	co_await transaction.query( std::format( "SET LOCAL hnsw.ef_search = {}", ef_search ) ).result();
	co_await transaction.query( "SET LOCAL enable_indexscan = off" ).result();

	const auto literal { embeddings::toHalfvecLiteral( *query_vector ) };
	const auto table { std::format( "embeddings_{}", model_id ) };

	db::DBBindings bindings {};
	bindings.bind( literal );
	const auto rows {
		co_await transaction
			.query(
				std::format(
					"SELECT record_id, embedding <=> $1::halfvec AS distance FROM {} ORDER BY distance LIMIT {}",
					table,
					limit ),
				std::move( bindings ) )
			.rows< RecordID, double >()
	};

	co_await transaction.commit();

	SearchEmbeddingsResponse response {
		{},
		{},
		static_cast< std::uint64_t >(
			std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - started )
				.count() )
	};
	response.record_ids.reserve( rows.size() );
	response.distances.reserve( rows.size() );
	for ( const auto& [ record_id, distance ] : rows )
	{
		response.record_ids.push_back( record_id );
		response.distances.push_back( distance );
	}
	co_return http::JsonResponse< SearchEmbeddingsResponse > { std::move( response ) };
}

http::HTTPTask< http::JsonResult< CompareEmbeddingsResponse > > compareEmbeddings(
	db::DBContext& db,
	http::RequestContext context )
{
	auto body_e { readValidatedJsonBody( context ) };
	if ( !body_e ) co_return std::move( body_e.error() );

	simdjson::dom::parser parser;
	simdjson::dom::element root {};
	try
	{
		root = parser.parse( *body_e );
	}
	catch ( const simdjson::simdjson_error& e )
	{
		co_return http::Response::error( 400, e.what() );
	}

	if ( !root.is_object() ) co_return http::Response::error( 400, "Expected a JSON body" );

	std::string_view model_name_sv {};
	if ( root[ "model_name" ].get_string().get( model_name_sv ) != simdjson::SUCCESS )
		co_return http::Response::error( 400, "Expected a string \"model_name\"" );
	const std::string model_name { model_name_sv };

	simdjson::dom::element ids_json {};
	if ( root[ "record_ids" ].get( ids_json ) != simdjson::SUCCESS || !ids_json.is_array() )
		co_return http::Response::error( 400, "Expected an array \"record_ids\"" );

	simdjson::dom::array ids_array {};
	if ( ids_json.get_array().get( ids_array ) != simdjson::SUCCESS )
		co_return http::Response::error( 500, "Unexpected: array conversion failed after is_array() check" );

	if ( ids_array.size() == 0 ) co_return http::Response::error( 400, "A comparison needs at least one record" );

	if ( ids_array.size() > max_compare_records )
		co_return http::Response::error(
			400,
			std::format(
				"At most {} records may be compared at once; {} were given", max_compare_records, ids_array.size() ) );

	std::vector< RecordID > record_ids {};
	record_ids.reserve( ids_array.size() );

	for ( const simdjson::dom::element entry : ids_array )
	{
		auto id { parsePositiveId< RecordID >( entry, "record_id" ) };
		if ( !id ) co_return std::move( id.error() );

		record_ids.push_back( *id );
	}

	std::vector< embeddings::QueryTerm > terms {};

	simdjson::dom::element terms_json {};
	if ( root[ "terms" ].get( terms_json ) == simdjson::SUCCESS && !terms_json.is_null() )
	{
		simdjson::dom::array terms_array {};
		if ( terms_json.get_array().get( terms_array ) != simdjson::SUCCESS )
			co_return http::Response::error( 400, "Expected an array \"terms\"" );

		if ( terms_array.size() > max_compare_terms )
			co_return http::Response::error(
				400,
				std::format(
					"At most {} terms may be compared at once; {} were given",
					max_compare_terms,
					terms_array.size() ) );

		auto parsed { parseQueryTerms( terms_json ) };
		if ( !parsed ) co_return std::move( parsed.error() );
		terms = std::move( *parsed );
	}

	const auto model { co_await embeddings::findEmbeddingModel( model_name, db ) };
	if ( !model )
		co_return http::Response::error( 404, std::format( "The model \"{}\" is not registered", model_name ) );

	const auto model_id { model->id };
	const auto dimensions { static_cast< std::size_t >( model->dimensions ) };

	const bool has_text_term { std::ranges::any_of( terms, []( const auto& term ) { return term.m_is_text; } ) };

	std::shared_ptr< modules::RemoteModule > module {};

	if ( has_text_term )
	{
		module = modules::ModuleLoader::instance().getEmbedderFor( model_name );

		if ( !module )
			co_return http::Response::error(
				404,
				std::format(
					"No loaded module provides the model \"{}\", so text terms cannot be embedded", model_name ) );

		if ( !module->supportsText() )
			co_return http::Response::error(
				400, std::format( "The model \"{}\" has no text encoder; use record references instead", model_name ) );
	}

	const auto started { std::chrono::steady_clock::now() };

	const auto table { std::format( "embeddings_{}", model_id ) };

	db::DBBindings present_bindings {};
	present_bindings.bind( record_ids );
	const auto present_rows {
		co_await db
			.query(
				std::format( "SELECT record_id FROM {} WHERE record_id = ANY($1)", table ),
				std::move( present_bindings ) )
			.rows< RecordID >()
	};

	std::unordered_set< RecordID > present {};
	for ( const auto& [ record_id ] : present_rows ) present.insert( record_id );

	for ( const auto record_id : record_ids )
	{
		if ( !present.contains( record_id ) )
			co_return http::Response::error(
				400,
				std::format(
					"Record {} has no embedding for this model. Run a backfill first, or choose another record",
					record_id ) );
	}

	auto resolved_e { co_await resolveQueryTerms( db, std::move( module ), model_id, std::move( terms ) ) };
	if ( !resolved_e ) co_return std::move( resolved_e.error() );
	const auto& resolved { *resolved_e };

	std::unordered_map< RecordID, std::size_t > column {};
	for ( std::size_t index = 0; index < record_ids.size(); ++index ) column.emplace( record_ids[ index ], index );

	std::vector< std::vector< double > > distances( resolved.size(), std::vector< double >( record_ids.size(), 0.0 ) );

	if ( !resolved.empty() )
	{
		std::vector< std::string > literals {};
		literals.reserve( resolved.size() );

		for ( const auto& term : resolved )
		{
			if ( term.m_vector.size() != dimensions )
				co_return http::Response::error(
					500,
					std::format(
						"A term resolved to {} values but the model has {}", term.m_vector.size(), dimensions ) );

			literals.push_back( embeddings::toHalfvecLiteral( term.m_vector ) );
		}

		db::DBBindings compare_bindings {};
		compare_bindings.bind( record_ids );
		compare_bindings.bind( literals );
		const auto rows {
			co_await db
				.query(
					std::format(
						"SELECT t.idx, e.record_id, e.embedding <=> t.vec::halfvec AS distance FROM {} e "
						"CROSS JOIN UNNEST($2::text[]) WITH ORDINALITY AS t(vec, idx) WHERE e.record_id = ANY($1)",
						table ),
					std::move( compare_bindings ) )
				.rows< std::int64_t, RecordID, double >()
		};

		for ( const auto& [ term_index_raw, record_id, distance ] : rows )
		{
			const auto term_index { static_cast< std::size_t >( term_index_raw - 1 ) };
			const auto found { column.find( record_id ) };
			if ( term_index >= distances.size() || found == column.end() ) continue;
			distances[ term_index ][ found->second ] = distance;
		}
	}

	std::optional< double > pair_distance {};

	if ( record_ids.size() >= 2 )
	{
		db::DBBindings pair_bindings {};
		pair_bindings.bind( record_ids[ 0 ] );
		pair_bindings.bind( record_ids[ 1 ] );
		const auto pair_rows {
			co_await db
				.query(
					std::format(
						"SELECT a.embedding <=> b.embedding AS distance FROM {} a, {} b "
						"WHERE a.record_id = $1 AND b.record_id = $2",
						table,
						table ),
					std::move( pair_bindings ) )
				.rows< double >()
		};

		if ( !pair_rows.empty() ) pair_distance = std::get< 0 >( pair_rows.row( 0 ) );
	}

	co_return http::JsonResponse< CompareEmbeddingsResponse > {
		{ std::move( record_ids ),
		  std::move( distances ),
		  http::possiblyMissing< double > { pair_distance },
		  static_cast< std::uint64_t >(
			  std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::steady_clock::now() - started )
				  .count() ) }
	};
}

} // namespace idhan::httpapi
