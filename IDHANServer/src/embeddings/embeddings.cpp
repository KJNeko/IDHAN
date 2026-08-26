#include "embeddings.hpp"

#include <drogon/drogon.h>

#include <format>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "db/drogonArrayBind.hpp"
#include "filesystem/filesystem.hpp"
#include "jobs/JobContext.hpp"
#include "logging/log.hpp"
#include "modules/ModuleLoader.hpp"
#include "modules/RemoteModule.hpp"

namespace idhan::embeddings
{

IDHANTask< std::optional< EmbeddingModelInfo > > findEmbeddingModel( const std::string_view model_name, DbClientPtr db )
{
	const auto rows { co_await db->execSqlCoro(
		"SELECT model_id, model_dimensions FROM embedding_models WHERE model_name = $1", model_name ) };

	if ( rows.empty() ) co_return std::nullopt;

	co_return EmbeddingModelInfo {
		.id = rows[ 0 ][ "model_id" ].as< std::int32_t >(),
		.dimensions = rows[ 0 ][ "model_dimensions" ].as< std::int32_t >()
	};
}

//! model_ids with a backfill currently running.
std::mutex g_running_mutex {};
std::unordered_set< std::int32_t > g_running {};

//! Releases a model's backfill claim when the job's frame is destroyed, however it ends.
struct RunningGuard
{
	std::int32_t m_model_id;

	RunningGuard( const std::int32_t model_id ) : m_model_id( model_id ) {}

	RunningGuard( const RunningGuard& ) = delete;
	RunningGuard& operator=( const RunningGuard& ) = delete;
	RunningGuard( RunningGuard&& ) = delete;
	RunningGuard& operator=( RunningGuard&& ) = delete;

	~RunningGuard()
	{
		const std::lock_guard< std::mutex > lock { g_running_mutex };
		g_running.erase( m_model_id );
	}
};

//! Records pulled from the database per sweep page.
constexpr std::size_t SWEEP_PAGE { 512 };

//! Rows per INSERT. Deliberately smaller than a sweep page: a 1152-dimension vector serialises to
//! ~13.8 KiB of text, so 128 rows is already ~1.8 MiB in one statement.
constexpr std::size_t INSERT_CHUNK { 128 };

//! Embed calls in flight at once. The module coalesces whatever arrives concurrently into one
//! forward pass, so this is what actually determines the batch size it gets to work with.
constexpr std::size_t IN_FLIGHT { 32 };

//! One record's pending work.
struct Candidate
{
	RecordID m_record_id { 0 };
	MimeID m_mime_id { 0 };
};

//! Formats a vector as the text pgvector parses into a halfvec.
[[nodiscard]] std::string toVectorLiteral( const std::vector< float >& values )
{
	std::string literal {};
	literal.reserve( values.size() * 12 + 2 );
	literal.push_back( '[' );

	for ( std::size_t index = 0; index < values.size(); ++index )
	{
		if ( index > 0 ) literal.push_back( ',' );
		literal += std::format( "{:.6g}", values[ index ] );
	}

	literal.push_back( ']' );
	return literal;
}

//! Embeds one record. Captureless by requirement, not by style.
drogon::Task< std::optional< std::pair< RecordID, std::string > > > embedOne(
	std::shared_ptr< modules::RemoteModule > module,
	RecordID record_id,
	MimeID mime_id,
	DbClientPtr db )
{
	auto input { co_await filesystem::openRecordInput( record_id, db ) };

	if ( !input )
	{
		log::warn( "Embedding: could not open record {} for reading", record_id );
		co_return std::nullopt;
	}

	modules::RemoteCallData call { .input = *input, .mime_id = mime_id, .extra = {}, .depth = 0 };

	const auto result { co_await module->embed( std::move( call ) ) };

	if ( !result )
	{
		// A corrupt or undecodable file is one record's problem, never the backfill's.
		log::warn( "Embedding: record {} failed: {}", record_id, result.error() );
		co_return std::nullopt;
	}

	co_return std::pair { record_id, toVectorLiteral( result->m_vector ) };
}

IDHANTask< void > registerEmbeddingModels( DbClientPtr db )
{
	const auto models { modules::ModuleLoader::instance().embeddingModels() };

	if ( models.empty() )
	{
		log::info( "No embedding models are available" );
		co_return;
	}

	for ( const auto& [ model_name, dimensions ] : models )
	{
		try
		{
			const auto existing { co_await findEmbeddingModel( model_name, db ) };

			if ( !existing )
			{
				const auto inserted { co_await db->execSqlCoro(
					"INSERT INTO embedding_models (model_name, model_dimensions) VALUES ($1, $2) RETURNING model_id",
					model_name,
					static_cast< std::int32_t >( dimensions ) ) };

				log::info(
					"Registered embedding model '{}' ({} dims) as model_id {}",
					model_name,
					dimensions,
					inserted[ 0 ][ "model_id" ].as< std::int32_t >() );

				continue;
			}

			if ( const auto stored { existing->dimensions }; stored != static_cast< std::int32_t >( dimensions ) )
			{
				log::error(
					"Embedding model '{}' is registered with {} dimensions but the loaded module produces {}. "
					"Refusing to use it. Re-export the model at {} dimensions, or register it under a new name.",
					model_name,
					stored,
					dimensions,
					stored );
			}
		}
		catch ( const std::exception& e )
		{
			log::error( "Failed to register embedding model '{}': {}", model_name, e.what() );
		}
	}
}

bool tryBeginBackfill( const std::int32_t model_id )
{
	const std::lock_guard< std::mutex > lock { g_running_mutex };
	return g_running.insert( model_id ).second;
}

void endBackfill( const std::int32_t model_id )
{
	const std::lock_guard< std::mutex > lock { g_running_mutex };
	g_running.erase( model_id );
}

JobTask backfillJob( const std::int32_t model_id, std::string model_name )
{
	const RunningGuard guard { model_id };

	auto db { drogon::app().getDbClient() };

	const auto module { modules::ModuleLoader::instance().getEmbedderFor( model_name ) };

	if ( module == nullptr )
	{
		Json::Value failure {};
		failure[ "error" ] = std::format( "no module provides model '{}'", model_name );
		co_await setJobResponse( failure );
		co_return;
	}

	const auto table { std::format( "embeddings_{}", model_id ) };

	std::size_t embedded { 0 };
	std::size_t failed { 0 };
	std::size_t skipped { 0 };
	RecordID cursor { 0 };

	const auto sweep { std::format(
		"SELECT fi.record_id as record_id, fi.mime_id as mime_id "
		"FROM file_info fi "
		"JOIN mime m USING (mime_id) "
		"WHERE fi.cluster_id IS NOT NULL "
		"  AND m.name LIKE 'image/%' "
		"  AND fi.record_id > $1 "
		"  AND NOT EXISTS ( SELECT 1 FROM {} e WHERE e.record_id = fi.record_id ) "
		"ORDER BY fi.record_id LIMIT {}",
		table,
		SWEEP_PAGE ) };

	const auto insert { std::format(
		"INSERT INTO {} (record_id, embedding) "
		"SELECT * FROM UNNEST($1::integer[], $2::text[]::halfvec[]) "
		"ON CONFLICT (record_id) DO UPDATE SET embedding = EXCLUDED.embedding",
		table ) };

	std::vector< RecordID > record_ids {};
	std::vector< std::string > vectors {};

	while ( true )
	{
		const auto page { co_await db->execSqlCoro( sweep, cursor ) };
		if ( page.empty() ) break;

		std::vector< Candidate > candidates {};
		candidates.reserve( page.size() );

		for ( const auto& row : page )
		{
			const auto record_id { row[ "record_id" ].as< RecordID >() };
			cursor = std::max( cursor, record_id );

			const auto mime_id { row[ "mime_id" ].as< MimeID >() };

			if ( modules::ModuleLoader::instance().getThumbnailerFor( mime_id ).empty() )
			{
				++skipped;
				continue;
			}

			candidates.emplace_back( Candidate { .m_record_id = record_id, .m_mime_id = mime_id } );
		}

		for ( std::size_t start = 0; start < candidates.size(); start += IN_FLIGHT )
		{
			const auto stop { std::min( start + IN_FLIGHT, candidates.size() ) };

			std::vector< drogon::Task< std::optional< std::pair< RecordID, std::string > > > > tasks {};
			tasks.reserve( stop - start );

			for ( std::size_t index = start; index < stop; ++index )
				tasks.emplace_back(
					embedOne( module, candidates[ index ].m_record_id, candidates[ index ].m_mime_id, db ) );

			auto results { co_await drogon::when_all( std::move( tasks ) ) };

			for ( auto& result : results )
			{
				if ( !result )
				{
					++failed;
					continue;
				}

				record_ids.emplace_back( result->first );
				vectors.emplace_back( std::move( result->second ) );
			}

			if ( record_ids.size() >= INSERT_CHUNK )
			{
				const auto written { record_ids.size() };
				co_await db->execSqlCoro(
					insert,
					std::forward< const std::vector< RecordID > >( record_ids ),
					std::forward< const std::vector< std::string > >( vectors ) );
				embedded += written;
				record_ids.clear();
				vectors.clear();

				Json::Value progress {};
				progress[ "model_name" ] = model_name;
				progress[ "embedded" ] = static_cast< Json::UInt64 >( embedded );
				progress[ "failed" ] = static_cast< Json::UInt64 >( failed );
				progress[ "skipped" ] = static_cast< Json::UInt64 >( skipped );
				co_await setJobResponse( progress );
			}
		}
	}

	if ( !record_ids.empty() )
	{
		const auto written { record_ids.size() };
		co_await db->execSqlCoro(
			insert,
			std::forward< const std::vector< RecordID > >( record_ids ),
			std::forward< const std::vector< std::string > >( vectors ) );
		embedded += written;
		record_ids.clear();
		vectors.clear();
	}

	Json::Value result {};
	result[ "model_name" ] = model_name;
	result[ "embedded" ] = static_cast< Json::UInt64 >( embedded );
	result[ "failed" ] = static_cast< Json::UInt64 >( failed );
	result[ "skipped" ] = static_cast< Json::UInt64 >( skipped );
	co_await setJobResponse( result );

	log::info(
		"Embedding backfill for '{}' finished: {} embedded, {} failed, {} skipped",
		model_name,
		embedded,
		failed,
		skipped );
}

} // namespace idhan::embeddings
