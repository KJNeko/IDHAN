#include "embeddings.hpp"

// Before drogon.h and before any execSqlCoro call: this supplies the SqlBinder specialisations that
// serialise a std::vector into PostgreSQL's binary array format, and an explicit specialisation has
// to be visible ahead of the instantiation that would otherwise pick the scalar template. clangd
// reports it as an unused include; it is not.
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

//! model_ids with a backfill currently running.
/** Process-local because jobs themselves are: their IDs are process-local and reset on restart, so a
 *  database-backed guard would outlive the thing it guards. */
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
	std::string m_mime {};
};

//! Formats a vector as the text pgvector parses into a halfvec.
/** Six significant digits: halfvec is fp16 and carries about 3.3 decimal digits, so this is already
 *  well past the point where the column could tell the difference. */
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
/** drogon::Task has a suspended initial_suspend, so a coroutine parked in a vector for when_all does
 *  not begin running until the loop that built it has finished. A capturing lambda's closure is dead
 *  by then; parameters are copied into the coroutine frame, so everything travels as a parameter. */
drogon::Task< std::optional< std::pair< RecordID, std::string > > > embedOne(
	std::shared_ptr< modules::RemoteModule > module,
	RecordID record_id,
	std::string mime,
	DbClientPtr db )
{
	auto input { co_await filesystem::openRecordInput( record_id, db ) };

	if ( !input )
	{
		log::warn( "Embedding: could not open record {} for reading", record_id );
		co_return std::nullopt;
	}

	modules::RemoteCallData call { .input = *input, .mime_name = std::move( mime ), .extra = {}, .depth = 0 };

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
			const auto existing { co_await db->execSqlCoro(
				"SELECT model_id, model_dimensions FROM embedding_models WHERE model_name = $1", model_name ) };

			if ( existing.empty() )
			{
				// The insert trigger creates embeddings_<model_id> at this width.
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

			if ( const auto stored { existing[ 0 ][ "model_dimensions" ].as< std::int32_t >() };
			     stored != static_cast< std::int32_t >( dimensions ) )
			{
				// Silently accepting this would write vectors of one width into a column of another,
				// which pgvector rejects per row -- an error storm during a backfill instead of one
				// line here.
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
	// Lives in the coroutine frame, so the claim is released whether this returns normally, returns
	// early on a missing module, or unwinds.
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

	// Keyset pagination, not OFFSET. The anti-join's result set shrinks as rows are inserted, so an
	// offset would step over records it had already shifted past.
	//
	// image/* only. The model needs pixels squashed to exactly its input geometry, and the vips
	// thumbnailer -- which covers every image/* MIME and nothing else -- is the only one that honours
	// the resize_mode "force" the module asks for. A video or a PSD comes back aspect-preserved at the
	// wrong size, which is a per-record error at best and, if the sizes ever happened to line up, a
	// silently wrong vector.
	const auto sweep { std::format(
		"SELECT fi.record_id as record_id, m.name as mime_name "
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

	// Declared outside the page loop: a page rarely ends on an INSERT_CHUNK boundary, and rebuilding
	// these per page would silently drop whatever had accumulated below the threshold.
	std::vector< RecordID > record_ids {};
	std::vector< std::string > vectors {};

	// No lambda helper for the flush: a capturing lambda coroutine is the one shape that is actively
	// unsafe here (drogon::Task is lazy, so the closure can be dead before the body runs), and the
	// two call sites are three lines each. Duplicated deliberately.
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

			auto mime { row[ "mime_name" ].as< std::string >() };

			// The sweep already restricted this to image/*, but a thumbnailer library that failed to
			// load leaves those MIMEs uncovered anyway. The module decodes nothing itself, so without
			// a thumbnailer behind the callback every one of these is a round trip that cannot succeed.
			if ( modules::ModuleLoader::instance().getThumbnailerFor( mime ).empty() )
			{
				++skipped;
				continue;
			}

			candidates.emplace_back( Candidate { .m_record_id = record_id, .m_mime = std::move( mime ) } );
		}

		for ( std::size_t start = 0; start < candidates.size(); start += IN_FLIGHT )
		{
			const auto stop { std::min( start + IN_FLIGHT, candidates.size() ) };

			std::vector< drogon::Task< std::optional< std::pair< RecordID, std::string > > > > tasks {};
			tasks.reserve( stop - start );

			for ( std::size_t index = start; index < stop; ++index )
				tasks.emplace_back(
					embedOne( module, candidates[ index ].m_record_id, candidates[ index ].m_mime, db ) );

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
				// Forwarded as rvalues: the SqlBinder specialisations in drogonArrayBind.hpp that
				// serialise a vector into PostgreSQL's binary array format are declared for
				// rvalue references, and an lvalue silently falls through to drogon's scalar
				// binder instead.
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

	// Whatever is left below the chunk threshold. Without this the tail of the last page -- up to
	// INSERT_CHUNK - 1 records -- would be embedded at real cost and then thrown away.
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
