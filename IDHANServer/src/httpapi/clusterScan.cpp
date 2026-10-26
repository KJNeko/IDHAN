#include "clusterScan.hpp"

#include <folly/coro/Task.h>
#include <json/json.h>

#include <utility>

#include "jobs/JobContext.hpp"
#include "scanning/ScanProgress.hpp"
#include "scanning/clusterScan.hpp"

namespace idhan::httpapi
{

[[nodiscard]] scanning::ScanMode scanMode( const bool scan_missing, const bool rescan )
{
	if ( rescan ) return scanning::ScanMode::Always;

	if ( scan_missing ) return scanning::ScanMode::MissingOnly;

	return scanning::ScanMode::Disabled;
}

[[nodiscard]] scanning::ScanOptions scanOptions( const ClusterScanParams& params )
{
	return scanning::ScanOptions {
		.read_only = params.read_only,
		.verify_hash = params.verify_hash,
		.adopt_orphans = params.adopt_orphans,
		.mime = scanMode( params.scan_mime, params.rescan_mime ),
		.metadata = scanMode( params.scan_metadata, params.rescan_metadata ),
		.stop_on_fail = params.stop_on_fail,
		.track_missing_files = params.track_missing_files,
		.fix_extensions = params.fix_extensions,
		.concurrency = params.concurrency
	};
}

JobTask scanClusterJob(
	const ClusterID cluster_id,
	std::filesystem::path cluster_path,
	const ClusterScanParams params,
	db::DBContext& db )
{
	const auto progress { std::make_shared< scanning::ScanProgress >() };
	co_await setJobProgress( progress );
	const auto executor { getJobRuntime().executor() };
	scanning::ScanTotals totals {};
	try
	{
		totals = co_await folly::coro::co_withExecutor(
			executor,
			scanning::scanCluster( db, cluster_id, std::move( cluster_path ), scanOptions( params ), *progress ) );
	}
	catch ( ... )
	{
		progress->setPhase( "failed" );
		throw;
	}

	Json::Value failures { Json::arrayValue };
	for ( const auto& failure : totals.failures )
	{
		Json::Value entry;
		entry[ "path" ] = failure.path.string();
		entry[ "error" ] = failure.message;
		failures.append( std::move( entry ) );
	}

	Json::Value result;
	result[ "cluster_id" ] = cluster_id;
	result[ "scanned_count" ] = static_cast< Json::UInt64 >( totals.file_count );
	result[ "scanned_bytes" ] = static_cast< Json::UInt64 >( totals.byte_size );
	result[ "failed_count" ] = static_cast< Json::UInt64 >( totals.failed_count );
	result[ "failures" ] = std::move( failures );

	co_await setJobResponse( std::move( result ) );
}

} // namespace idhan::httpapi
