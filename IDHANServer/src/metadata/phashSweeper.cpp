#include <expected>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>

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

static runtime::Task< std::expected< bool, std::string > > backfillPhash(
	db::DBContext& db,
	db::MissingPhashRecord record,
	const std::stop_token stop,
	BackfillClusters& clusters )
{
	if ( !record.mime_id ) co_return std::unexpected( "Record has no MIME" );

	const auto parsers { modules::ModuleLoader::instance().getParserFor( *record.mime_id ) };
	if ( parsers.empty() ) co_return std::unexpected( "No metadata parser is available" );

	auto cached { clusters.find( record.cluster_id ) };
	if ( cached == clusters.end() )
	{
		auto opened { co_await filesystem::openCluster( db, record.cluster_id ) };
		cached = clusters.emplace( record.cluster_id, std::move( opened ) ).first;
	}
	auto& access { cached->second };
	if ( !access ) co_return std::unexpected( access.error() );

	const auto valid_before_read { access->verify() };
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

	const auto valid { access->verify() };
	if ( !valid ) co_return std::unexpected( valid.error() );

	const auto* image { std::get_if< MetadataInfoImage >( &parsed->m_metadata ) };
	if ( !image ) co_return std::unexpected( "Parser did not return image metadata" );

	if ( !image->phash ) co_return std::unexpected( "Parser did not return a perceptual hash" );

	co_return co_await db::updateMissingPhash( db, record, *image->phash );
}

runtime::Task<> runPhashSweep( db::DBContext& db, const std::stop_token stop, jobs::BackgroundJob* progress )
{
	const auto through { co_await db::missingPhashHighWater( db ) };
	if ( !through ) co_return;

	std::optional< RecordID > after;
	std::size_t generated {};
	while ( !stop.stop_requested() )
	{
		const auto records { co_await db::missingPhashBatch( db, after, *through, 8 ) };
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
					log::warn( "Missing phash backfill for record {}: {}", record.record_id, result.error() );
				}
				else if ( *result )
					++generated;
			}
			catch ( const std::exception& error )
			{
				if ( progress ) progress->recordFailure( error.what() );
				log::warn( "Missing phash backfill for record {}: {}", record.record_id, error.what() );
			}
			if ( progress ) progress->processed();
		}
	}
	if ( generated ) log::info( "Generated {} missing perceptual hashes", generated );
}
} // namespace idhan::metadata
