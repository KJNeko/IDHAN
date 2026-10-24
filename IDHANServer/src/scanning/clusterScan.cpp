#include "clusterScan.hpp"

#include <folly/coro/AsyncScope.h>
#include <folly/coro/BoundedQueue.h>
#include <folly/coro/CurrentExecutor.h>

#include <algorithm>
#include <atomic>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

#include "MimeIDs.hpp"
#include "ScanDirectory.hpp"
#include "db/fileInfo.hpp"
#include "db/records.hpp"
#include "filesystem/clusters/clusterIdentity.hpp"
#include "filesystem/hashFile.hpp"
#include "filesystem/renameNoReplace.hpp"
#include "logging/log.hpp"
#include "metadata/RecordMetadata.hpp"
#include "mime/identifyMime.hpp"

namespace idhan::scanning
{

struct ScanError
{
	std::string message;
	bool cluster_unavailable { false };
};

using ScanResult = std::expected< void, ScanError >;

struct ScanOutcome
{
	ScanCandidate candidate {};
	ScanResult result {};
};

struct ScanPlan
{
	bool detect_mime;
	bool parse_metadata;
	bool verify_hash;
};

[[nodiscard]] bool needsScan( const ScanMode mode, const bool present )
{
	if ( mode == ScanMode::Always ) return true;

	if ( mode == ScanMode::Disabled ) return false;

	return !present;
}

[[nodiscard]] ScanPlan planScan( const ScanOptions& options, const std::optional< db::RecordFileState >& state )
{
	return ScanPlan {
		needsScan( options.mime, state && state->mime_id.has_value() ),
		needsScan( options.metadata, state && state->has_metadata ),
		options.verify_hash || !state
	};
}

void recordFailure( ScanTotals& totals, const std::filesystem::path& path, const std::string& message )
{
	++totals.failed_count;
	if ( totals.failures.size() < 1000 ) totals.failures.push_back( ScanFailure { path, message } );
	log::warn( "Cluster scan failed for {}: {}", path.string(), message );
}

[[nodiscard]] std::string fileExtension( const std::filesystem::path& path )
{
	auto extension { path.extension().string() };
	if ( extension.starts_with( '.' ) ) extension.erase( 0, 1 );
	return extension;
}

[[nodiscard]] ScanResult moveWithoutReplacing( const std::filesystem::path& from, const std::filesystem::path& to )
{
	std::error_code error {};
	std::filesystem::create_directories( to.parent_path(), error );
	if ( error )
		return std::unexpected(
			ScanError { std::format( "Cannot create {}: {}", to.parent_path().string(), error.message() ) } );

	const auto renamed { filesystem::renameNoReplace( from, to ) };
	if ( !renamed )
		return std::unexpected(
			ScanError {
				std::format( "Cannot move {} to {}: {}", from.string(), to.string(), renamed.error().message() ) } );

	return {};
}

[[nodiscard]] runtime::Task< std::expected< bool, std::string > > preflightMissingFiles(
	db::DBContext& db,
	const ClusterID cluster_id,
	const ScanOptions& options,
	const filesystem::ClusterAccess& access,
	ScanTotals& totals )
{
	if ( !options.track_missing_files ) co_return false;

	RecordID after_record_id {};
	while ( true )
	{
		const auto valid { access.verify() };
		if ( !valid ) co_return std::unexpected( valid.error() );

		const auto files { co_await db::clusterRecordFiles( db, cluster_id, after_record_id, 1024 ) };
		if ( files.empty() ) co_return false;

		for ( const auto& file : files )
		{
			after_record_id = file.record_id;
			const auto path { access.root() / filesystem::getClusterRelativePath( file.hash, file.extension ) };
			std::error_code error {};
			const auto status { std::filesystem::status( path, error ) };
			if ( error == std::errc::no_such_file_or_directory ) error.clear();
			if ( error )
				co_return std::unexpected( std::format( "Cannot inspect {}: {}", path.string(), error.message() ) );

			if ( std::filesystem::is_regular_file( status ) ) continue;

			const auto still_valid { access.verify() };
			if ( !still_valid ) co_return std::unexpected( still_valid.error() );

			co_await db::detachMissingRecordFile( db, file.record_id, cluster_id );
			recordFailure( totals, path, std::format( "Cluster {} is missing record {}", cluster_id, file.record_id ) );
			if ( options.stop_on_fail ) co_return true;
		}
	}
}

[[nodiscard]] runtime::Task< ScanResult > checkAssignedFile(
	db::DBContext& db,
	const db::RecordFileState& state,
	const ClusterID cluster_id,
	const SHA256& hash )
{
	if ( !state.cluster_id ) co_return ScanResult {};

	if ( *state.cluster_id == cluster_id ) co_return ScanResult {};

	auto assigned { co_await filesystem::openCluster( db, *state.cluster_id ) };
	if ( !assigned ) co_return std::unexpected( ScanError { assigned.error(), true } );

	const auto extension { state.extension.value_or( state.best_extension.value_or( "" ) ) };
	const auto path { assigned->root() / filesystem::getClusterRelativePath( hash, extension ) };
	std::error_code error {};
	const auto status { std::filesystem::status( path, error ) };
	if ( error == std::errc::no_such_file_or_directory ) error.clear();
	if ( error )
		co_return std::unexpected(
			ScanError { std::format( "Cannot inspect assigned file {}: {}", path.string(), error.message() ) } );

	// Preserve both copies until an operator chooses which one to remove.
	if ( std::filesystem::is_regular_file( status ) )
		co_return std::unexpected(
			ScanError {
				std::format( "Record {} is already stored in cluster {}", state.record_id, *state.cluster_id ) } );

	co_return ScanResult {};
}

[[nodiscard]] bool fileInfoMatches(
	const db::RecordFileState& state,
	const ClusterID cluster_id,
	const std::int64_t size,
	const std::optional< MimeID > mime_id,
	const std::string& extension,
	const filesystem::FileTimes& times )
{
	return state.cluster_id == cluster_id && state.size == size && state.mime_id == mime_id
	    && state.extension == extension && state.mtime_us == filesystem::toMicroseconds( times.mtime )
	    && state.btime_us == filesystem::toMicroseconds( times.btime ) && state.stored;
}

[[nodiscard]] runtime::Task< ScanResult > scanFile(
	db::DBContext& db,
	const ClusterID cluster_id,
	ScanCandidate candidate,
	const ScanOptions& options,
	const filesystem::ClusterAccess& access )
{
	if ( candidate.size == 0 )
		co_return std::unexpected( ScanError { std::format( "Empty file: {}", candidate.path.string() ) } );

	if ( candidate.size > static_cast< std::uint64_t >( std::numeric_limits< std::int64_t >::max() ) )
		co_return std::unexpected(
			ScanError { std::format( "File is too large for the database: {}", candidate.path.string() ) } );

	const auto filename_hash { SHA256::fromHex( candidate.path.stem().string() ) };
	if ( !filename_hash )
		co_return std::unexpected(
			ScanError { std::format( "Invalid hash in filename: {}", candidate.path.string() ) } );

	const auto hash { *filename_hash };
	auto extension { fileExtension( candidate.path ) };
	const auto canonical_path { access.root() / filesystem::getClusterRelativePath( hash, extension ) };
	if ( options.read_only && candidate.path != canonical_path )
		co_return std::unexpected(
			ScanError { std::format( "File is outside its hash directory: {}", candidate.path.string() ) } );

	const auto valid { access.verify() };
	if ( !valid ) co_return std::unexpected( ScanError { valid.error(), true } );

	const auto state { co_await db::findRecordFileState( db, hash ) };
	if ( !state && !options.adopt_orphans )
		co_return std::unexpected( ScanError { std::format( "No record exists for {}", candidate.path.string() ) } );

	if ( state )
	{
		auto assigned { co_await checkAssignedFile( db, *state, cluster_id, hash ) };
		if ( !assigned ) co_return std::unexpected( std::move( assigned.error() ) );
	}

	const auto plan { planScan( options, state ) };
	if ( plan.verify_hash )
	{
		const auto actual { co_await filesystem::hashFile( candidate.path ) };
		if ( actual != hash )
			co_return std::unexpected(
				ScanError { std::format( "Filename does not match SHA-256: {}", candidate.path.string() ) } );
	}

	auto mime_id { state ? state->mime_id : std::optional< MimeID > {} };
	if ( plan.detect_mime )
	{
		const auto detected { co_await mime::identifyMimeForPath( candidate.path ) };
		mime_id = detected == mime_ids::UNKNOWN ? std::nullopt : std::optional< MimeID > { detected };
	}

	if ( mime_id && options.fix_extensions && !options.read_only )
	{
		auto best {
			state && state->mime_id == mime_id ? state->best_extension : co_await db::bestMimeExtension( db, *mime_id )
		};
		if ( best && !best->empty() ) extension = *best;
	}

	const auto destination { access.root() / filesystem::getClusterRelativePath( hash, extension ) };
	if ( candidate.path != destination )
	{
		const auto valid_before_move { access.verify() };
		if ( !valid_before_move ) co_return std::unexpected( ScanError { valid_before_move.error(), true } );

		auto moved { moveWithoutReplacing( candidate.path, destination ) };
		if ( !moved ) co_return std::unexpected( std::move( moved.error() ) );
		candidate.path = destination;
	}

	const auto times { filesystem::getFileTimes( candidate.path ) };
	if ( !times ) co_return std::unexpected( ScanError { std::format( "Cannot stat {}", candidate.path.string() ) } );

	const auto valid_before_update { access.verify() };
	if ( !valid_before_update ) co_return std::unexpected( ScanError { valid_before_update.error(), true } );

	RecordID record_id {};
	if ( state )
		record_id = state->record_id;
	else
	{
		auto created { co_await db::ensureRecord( hash, db ) };
		if ( !created ) co_return std::unexpected( ScanError { created.error() } );

		record_id = *created;
	}

	const auto size { static_cast< std::int64_t >( candidate.size ) };
	const bool unchanged { state && fileInfoMatches( *state, cluster_id, size, mime_id, extension, *times ) };
	if ( !unchanged ) co_await db::storeRecordFileInfo( db, record_id, cluster_id, size, mime_id, extension, *times );

	if ( !mime_id ) co_return ScanResult {};

	if ( !plan.parse_metadata ) co_return ScanResult {};

	auto parsed { co_await metadata::parseAndStoreRecordMetadata( db, record_id, *mime_id, access, candidate.path ) };
	if ( !parsed ) co_return std::unexpected( ScanError { parsed.error().message, parsed.error().status == 503 } );

	co_return ScanResult {};
}

// These references belong to scanCluster's frame. Its scope joins every worker before they can expire.
runtime::Task< void > scanOne(
	db::DBContext& db,
	const ClusterID cluster_id,
	ScanCandidate candidate,
	const ScanOptions& options,
	const filesystem::ClusterAccess& access,
	std::atomic< bool >& stopped,
	folly::coro::BoundedQueue< ScanOutcome >& completed )
{
	ScanResult result {};
	try
	{
		result = co_await scanFile( db, cluster_id, candidate, options, access );
	}
	catch ( const std::exception& error )
	{
		result = std::unexpected( ScanError { error.what() } );
	}

	// Publish the stop before the outcome so scheduling can observe it while consuming an earlier success.
	if ( !result )
	{
		if ( options.stop_on_fail || result.error().cluster_unavailable ) stopped.store( true );
	}
	co_await completed.enqueue( ScanOutcome { std::move( candidate ), std::move( result ) } );
}

runtime::Task< void > scheduleFiles(
	db::DBContext& db,
	const ClusterID cluster_id,
	const ScanOptions& options,
	const filesystem::ClusterAccess& access,
	folly::coro::AsyncScope& workers,
	folly::coro::BoundedQueue< ScanOutcome >& completed,
	std::atomic< bool >& stopped,
	ScanTotals& totals )
{
	ScanDirectory directory { access.root() };
	auto* executor { co_await folly::coro::co_current_executor };
	std::size_t active {};
	bool exhausted { false };
	while ( true )
	{
		while ( !exhausted && !stopped.load() && active < options.concurrency )
		{
			const auto valid { access.verify() };
			if ( !valid )
			{
				recordFailure( totals, access.root(), valid.error() );
				stopped.store( true );
				break;
			}

			auto candidate { directory.next() };
			if ( !candidate )
			{
				recordFailure( totals, candidate.error().path, candidate.error().message );
				if ( options.stop_on_fail ) stopped.store( true );
				continue;
			}

			if ( !*candidate )
			{
				exhausted = true;
				break;
			}

			if ( stopped.load() ) break;

			workers.add(
				folly::coro::co_withExecutor(
					executor,
					scanOne( db, cluster_id, std::move( **candidate ), options, access, stopped, completed ) ) );
			++active;
		}

		if ( active == 0 ) co_return;

		const auto outcome { co_await completed.dequeue() };
		--active;
		++totals.file_count;
		totals.byte_size += outcome.candidate.size;
		if ( !outcome.result ) recordFailure( totals, outcome.candidate.path, outcome.result.error().message );
	}
}

runtime::Task< ScanTotals > scanCluster(
	db::DBContext& db,
	const ClusterID cluster_id,
	std::filesystem::path configured_path,
	ScanOptions options )
{
	ScanTotals totals {};
	auto access { co_await filesystem::openCluster( db, cluster_id ) };
	if ( !access )
	{
		recordFailure( totals, configured_path, access.error() );
		co_return totals;
	}

	// Recheck the stored flag when the job starts, rather than trusting only the flag observed at enqueue time.
	options.read_only = options.read_only || access->readonly();
	options.concurrency = std::clamp( options.concurrency, std::size_t { 1 }, std::size_t { 64 } );
	const auto preflight { co_await preflightMissingFiles( db, cluster_id, options, *access, totals ) };
	if ( !preflight )
	{
		recordFailure( totals, configured_path, preflight.error() );
		co_return totals;
	}

	if ( *preflight ) co_return totals;

	folly::coro::AsyncScope workers {};
	folly::coro::BoundedQueue< ScanOutcome > completed { static_cast< std::uint32_t >( options.concurrency ) };
	std::atomic< bool > stopped { false };
	// Join on exceptional exits too; workers retain references to the queue, stop flag and pinned cluster.
	auto scheduled { co_await folly::coro::co_awaitTry(
		scheduleFiles( db, cluster_id, options, *access, workers, completed, stopped, totals ) ) };
	co_await workers.joinAsync();
	scheduled.throwIfFailed();
	co_return totals;
}

} // namespace idhan::scanning
