#include <drogon/drogon.h>

#include <algorithm>
#include <atomic>
#include <span>

#include "MetadataInfo.hpp"
#include "logging/log.hpp"
#include "metadata.hpp"
#include "phash.hpp"
#include "threading/SingleRunGuard.hpp"

namespace idhan::metadata
{

static std::atomic< bool > sweep_running { false };

static drogon::Task< void > processPhashBatch( DbClientPtr db, std::vector< RecordID > candidates )
{
	const auto records { co_await findMissingPhashes( db, std::move( candidates ) ) };
	std::size_t processed_count { 0 };
	for ( const auto record_id : records )
	{
		try
		{
			const auto parsed { co_await parseMetadata( record_id, db ) };
			if ( !parsed )
			{
				log::warn( "Could not generate missing phash for record {}: {}", record_id, parsed.error()->body() );
				continue;
			}

			const auto* image { std::get_if< MetadataInfoImage >( &parsed->m_metadata ) };
			if ( image == nullptr ) continue;

			if ( !image->phash ) continue;

			co_await updateMissingPhash( record_id, db, *image->phash );
			++processed_count;
		}
		catch ( const std::exception& e )
		{
			log::warn( "Could not generate missing phash for record {}: {}", record_id, e.what() );
		}
	}

	if ( !records.empty() )
	{
		const auto remaining { co_await countMissingPhashes( db ) };
		log::info(
			"Missing phash sweep processed {} records, {} phashes remaining to process", processed_count, remaining );
	}
}

drogon::Task< void > runPhashSweep( DbClientPtr db )
{
	SingleRunGuard guard { sweep_running };
	if ( !guard.acquired() ) co_return;

	const auto records { co_await findMissingPhashes( db ) };
	const std::span< const RecordID > pending { records };
	for ( std::size_t offset { 0 }; offset < pending.size(); )
	{
		const auto batch {
			pending.subspan( offset, std::min< std::size_t >( PHASH_BATCH_SIZE, pending.size() - offset ) )
		};
		std::vector< RecordID > candidates { batch.begin(), batch.end() };
		co_await processPhashBatch( db, std::move( candidates ) );
		offset += batch.size();
	}
}

void startPhashSweeper()
{
	drogon::app().getLoop()->runEvery(
		PHASH_INTERVAL,
		[]()
		{
			drogon::async_run(
				[]() -> drogon::Task< void >
				{
					try
					{
						co_await runPhashSweep( drogon::app().getDbClient() );
					}
					catch ( const std::exception& e )
					{
						log::warn( "Missing phash sweep failed: {}", e.what() );
					}

					co_return;
				} );
		} );
}

} // namespace idhan::metadata
