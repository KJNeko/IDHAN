#include <expected>
#include <format>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "db/hashMaintenance.hpp"
#include "filesystem/PathHelpers.hpp"
#include "filesystem/clusters/clusterIdentity.hpp"
#include "idhan/db/DBContext.hpp"
#include "jobs/BackgroundJob.hpp"
#include "logging/log.hpp"
#include "modules/ModuleLoader.hpp"
#include "modules/RemoteModule.hpp"
#include "phash.hpp"

namespace idhan::metadata
{

// Confined to one sequential eight-record batch. Both successful access and failures
// are reused; the next batch reloads cluster configuration and retries failed opens.
using BackfillClusters = std::unordered_map< ClusterID, std::expected< filesystem::ClusterAccess, std::string > >;

struct SweepFailure
{
	std::size_t count {};
	std::vector< RecordID > examples {};
};

//! Failure reasons for one sweep, so a run that skips many records produces a single warning.
using SweepFailures = std::map< std::string, SweepFailure >;

static void noteFailure( SweepFailures& failures, const RecordID record, std::string reason )
{
	constexpr std::size_t max_examples { 5 };

	auto& failure { failures[ std::move( reason ) ] };
	++failure.count;

	if ( failure.examples.size() < max_examples ) failure.examples.push_back( record );
}

static void reportFailures( const SweepFailures& failures )
{
	if ( failures.empty() ) return;

	std::size_t total {};
	std::string lines {};

	for ( const auto& [ reason, failure ] : failures )
	{
		total += failure.count;

		std::string examples {};

		for ( const auto record : failure.examples )
		{
			if ( !examples.empty() ) examples += ", ";
			examples += std::to_string( record );
		}

		lines += std::format( "\n  {} record(s): {} (e.g. {})", failure.count, reason, examples );
	}

	log::warn( "Missing phash backfill skipped {} record(s):{}", total, lines );
}

static runtime::Task< std::expected< bool, std::string > > backfillPhash(
	db::DBContext& db,
	db::MissingPhashRecord record,
	const std::stop_token stop,
	BackfillClusters& clusters )
{
	if ( !record.mime_id ) co_return std::unexpected( "Record has no MIME" );

	const auto parsers { modules::ModuleLoader::instance().getPhashParserFor( *record.mime_id ) };
	if ( parsers.empty() ) co_return std::unexpected( "No metadata parser is available" );

	auto cached { clusters.find( record.cluster_id ) };
	if ( cached == clusters.end() )
	{
		auto opened { co_await filesystem::openCluster( db, record.cluster_id ) };
		cached = clusters.emplace( record.cluster_id, std::move( opened ) ).first;
	}
	auto& access { cached->second };
	if ( !access ) co_return std::unexpected( access.error() );

	const auto valid_before_read { access->verifySync() };
	if ( !valid_before_read ) co_return std::unexpected( valid_before_read.error() );

	auto input { modules::CallInput::forPath(
		access->root() / filesystem::getClusterRelativePath( record.sha256, record.extension ) ) };
	if ( !input ) co_return std::unexpected( input.error() );

	const modules::RemoteCallData call {
		.input = std::make_shared< const modules::CallInput >( std::move( *input ) ),
		.mime_id = *record.mime_id,
		.extra = {},
		.depth = 0
	};
	const auto parsed { co_await parsers.front()->parseFile( call ) };
	if ( !parsed ) co_return std::unexpected( parsed.error() );

	if ( stop.stop_requested() ) co_return false;

	const auto valid { access->verifySync() };
	if ( !valid ) co_return std::unexpected( valid.error() );

	const auto* image { std::get_if< MetadataInfoImage >( &parsed->m_metadata ) };
	if ( !image ) co_return std::unexpected( "Parser did not return image metadata" );

	if ( !image->phash ) co_return std::unexpected( "Parser did not return a perceptual hash" );

	co_return co_await db::updateMissingPhash( db, record, *image->phash );
}

runtime::Task<> runPhashSweep( db::DBContext& db, const std::stop_token stop, jobs::BackgroundJob* progress )
{
	const auto phash_mimes { modules::ModuleLoader::instance().phashMimes() };
	if ( phash_mimes.empty() ) co_return;

	const auto through { co_await db::missingPhashHighWater( db, phash_mimes ) };
	if ( !through ) co_return;

	std::optional< RecordID > after;
	std::size_t generated {};
	SweepFailures failures {};
	while ( !stop.stop_requested() )
	{
		const auto records { co_await db::missingPhashBatch( db, after, *through, 8, phash_mimes ) };
		if ( records.empty() ) break;
		BackfillClusters clusters {};
		for ( const auto& record : records )
		{
			if ( stop.stop_requested() ) break;
			after = record.record_id;
			if ( progress )
				progress->activity(
					"working", std::format( "Parsing perceptual hash for record {}", record.record_id ) );
			try
			{
				const auto result { co_await backfillPhash( db, record, stop, clusters ) };
				if ( !result )
				{
					if ( progress ) progress->recordFailure( result.error() );
					log::debug( "Missing phash backfill for record {}: {}", record.record_id, result.error() );
					noteFailure( failures, record.record_id, result.error() );
				}
				else if ( *result )
					++generated;
			}
			catch ( const std::exception& error )
			{
				if ( progress ) progress->recordFailure( error.what() );
				log::debug( "Missing phash backfill for record {}: {}", record.record_id, error.what() );
				noteFailure( failures, record.record_id, error.what() );
			}
			if ( progress ) progress->processed();
		}
	}
	reportFailures( failures );

	if ( generated ) log::info( "Generated {} missing perceptual hashes", generated );
}
} // namespace idhan::metadata
