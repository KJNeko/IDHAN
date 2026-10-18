#include "clusterScan.hpp"

#include <folly/coro/Collect.h>
#include <folly/coro/Task.h>
#include <folly/io/IOBuf.h>
#include <json/json.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "MimeIDs.hpp"
#include "fileMetadata.hpp"
#include "filesystem/PathHelpers.hpp"
#include "filesystem/io/IOUring.hpp"
#include "filesystem/renameNoReplace.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/SHA256Hasher.hpp"
#include "idhan/db/DBContext.hpp"
#include "jobs/JobContext.hpp"
#include "logging/log.hpp"
#include "mime/identifyMime.hpp"
#include "records.hpp"

namespace idhan::httpapi
{

using ScanResult = std::expected< void, std::string >;

struct ScanTotals
{
	std::uint64_t file_count {};
	std::uint64_t byte_size {};
	std::uint64_t failed_count {};
	Json::Value failures { Json::arrayValue };
};

struct ScanOutcome
{
	std::filesystem::path path;
	std::uint64_t size;
	ScanResult result;
};

void recordFailure( ScanTotals& totals, const std::filesystem::path& path, const std::string& message );

[[nodiscard]] std::string responseText( const http::Response& response )
{
	const auto* body { std::get_if< std::unique_ptr< folly::IOBuf > >( &response.body ) };
	if ( !body || !*body ) return "Unknown error";
	return ( *body )->to< std::string >();
}

[[nodiscard]] runtime::Task< SHA256 > hashFile( const std::filesystem::path& path )
{
	FileIOUring file { path };
	SHA256Hasher hasher;
	constexpr std::size_t chunk_size { 1024 * 1024 };
	std::size_t offset {};
	while ( offset < file.size() )
	{
		const auto chunk { co_await file.read( offset, std::min( chunk_size, file.size() - offset ) ) };
		if ( chunk.empty() ) throw std::runtime_error( std::format( "Short read while hashing {}", path.string() ) );
		hasher.update( std::span< const std::byte > { chunk } );
		offset += chunk.size();
	}
	co_return hasher.finish();
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
		return std::unexpected( std::format( "Cannot create {}: {}", to.parent_path().string(), error.message() ) );

	const auto renamed { filesystem::renameNoReplace( from, to ) };
	if ( !renamed )
		return std::unexpected(
			std::format( "Cannot move {} to {}: {}", from.string(), to.string(), renamed.error().message() ) );

	return {};
}

[[nodiscard]] runtime::Task< std::expected< bool, std::string > > preflightMissingFiles(
	db::DBContext& db,
	const ClusterID cluster_id,
	const std::filesystem::path& root,
	const ClusterScanParams& params,
	ScanTotals& totals )
{
	if ( !params.track_missing_files ) co_return false;

	db::DBBindings bindings {};
	bindings.bind( cluster_id );
	const auto rows {
		co_await db
			.query(
				"SELECT fi.record_id, r.sha256, COALESCE(fi.extension, m.best_extension, '') "
				"FROM file_info fi JOIN records r USING (record_id) "
				"LEFT JOIN mime m USING (mime_id) WHERE fi.cluster_id = $1",
				std::move( bindings ) )
			.rows< RecordID, std::span< const std::byte >, std::string >()
	};

	for ( const auto& [ record_id, hash_bytes, extension ] : rows )
	{
		const SHA256 hash { SHA256::fromBuffer( hash_bytes ) };
		const auto path { root / filesystem::getClusterRelativePath( hash, extension ) };
		std::error_code error {};
		const auto status { std::filesystem::status( path, error ) };
		if ( error == std::errc::no_such_file_or_directory ) error.clear();
		if ( error )
			co_return std::unexpected( std::format( "Cannot inspect {}: {}", path.string(), error.message() ) );
		if ( std::filesystem::is_regular_file( status ) ) continue;

		db::DBBindings missing_bindings {};
		missing_bindings.bind( record_id );
		missing_bindings.bind( cluster_id );
		co_await db
			.query(
				"UPDATE file_info SET cluster_id = NULL WHERE record_id = $1 AND cluster_id = $2 "
				"AND cluster_delete_time IS NULL",
				std::move( missing_bindings ) )
			.result();
		recordFailure( totals, path, std::format( "Cluster {} is missing record {}", cluster_id, record_id ) );
		if ( params.stop_on_fail ) co_return true;
	}

	co_return false;
}

[[nodiscard]] runtime::Task< ScanResult > scanFile(
	db::DBContext& db,
	const ClusterID cluster_id,
	const std::filesystem::path& root,
	std::filesystem::path path,
	const std::uint64_t size,
	const ClusterScanParams& params )
{
	if ( size == 0 ) co_return std::unexpected( std::format( "Empty file: {}", path.string() ) );
	if ( size > static_cast< std::uint64_t >( std::numeric_limits< std::int64_t >::max() ) )
		co_return std::unexpected( std::format( "File is too large for the database: {}", path.string() ) );

	const auto filename_hash { SHA256::fromHex( path.stem().string() ) };
	if ( !filename_hash && !params.adopt_orphans && !params.verify_hash )
		co_return std::unexpected( std::format( "Invalid hash in filename: {}", path.string() ) );

	SHA256 hash {};
	if ( params.verify_hash || !filename_hash )
	{
		hash = co_await hashFile( path );
		if ( path.stem().string() != hash.hex() )
			co_return std::unexpected( std::format( "Filename does not match SHA-256: {}", path.string() ) );
	}
	else
		hash = *filename_hash;

	auto record_id { co_await findRecord( hash, db ) };
	if ( !record_id )
	{
		if ( !params.adopt_orphans )
			co_return std::unexpected( std::format( "No record exists for {}", path.string() ) );
		if ( !params.verify_hash )
		{
			const auto actual_hash { co_await hashFile( path ) };
			if ( actual_hash != hash )
				co_return std::unexpected( std::format( "Orphan filename does not match SHA-256: {}", path.string() ) );
		}
		auto created { co_await createRecord( hash, db ) };
		if ( !created ) co_return std::unexpected( responseText( created.error() ) );
		record_id = *created;
	}

	db::DBBindings lookup_bindings {};
	lookup_bindings.bind( *record_id );
	const auto info {
		co_await db
			.query(
				"SELECT fi.cluster_id, fi.mime_id, fc.folder_path, COALESCE(fi.extension, m.best_extension, '') "
				"FROM file_info fi LEFT JOIN file_clusters fc ON fc.cluster_id = fi.cluster_id "
				"LEFT JOIN mime m ON m.mime_id = fi.mime_id WHERE fi.record_id = $1",
				std::move( lookup_bindings ) )
			.rows< std::optional< ClusterID >, std::optional< MimeID >, std::optional< std::string >, std::string >()
	};
	std::optional< MimeID > mime_id {};
	if ( !info.empty() )
	{
		const auto& [ assigned_cluster, existing_mime, assigned_root, assigned_extension ] { info.row( 0 ) };
		mime_id = existing_mime;
		if ( assigned_cluster && *assigned_cluster != cluster_id && assigned_root )
		{
			const auto assigned_path { std::filesystem::path { *assigned_root }
				                       / filesystem::getClusterRelativePath( hash, assigned_extension ) };
			std::error_code assigned_error {};
			const auto assigned_status { std::filesystem::status( assigned_path, assigned_error ) };
			if ( assigned_error == std::errc::no_such_file_or_directory ) assigned_error.clear();
			if ( assigned_error )
				co_return std::unexpected(
					std::format(
						"Cannot inspect assigned file {}: {}", assigned_path.string(), assigned_error.message() ) );
			// Keep both physical copies until an operator chooses which one to remove.
			if ( std::filesystem::is_regular_file( assigned_status ) )
				co_return std::unexpected(
					std::format( "Record {} is already stored in cluster {}", *record_id, *assigned_cluster ) );
		}
	}

	const auto expected_path { root / filesystem::getClusterRelativePath( hash, fileExtension( path ) ) };
	if ( path != expected_path )
	{
		if ( params.read_only )
			co_return std::unexpected( std::format( "File is outside its hash directory: {}", path.string() ) );
		auto moved { moveWithoutReplacing( path, expected_path ) };
		if ( !moved ) co_return std::unexpected( std::move( moved.error() ) );
		path = expected_path;
	}

	if ( ( params.scan_mime && !mime_id ) || params.rescan_mime )
	{
		const auto detected { co_await mime::identifyMimeForPath( path ) };
		mime_id = detected == mime_ids::UNKNOWN ? std::nullopt : std::optional< MimeID > { detected };
	}

	if ( mime_id && params.fix_extensions && !params.read_only )
	{
		db::DBBindings extension_bindings {};
		extension_bindings.bind( *mime_id );
		const auto extensions {
			co_await db.query( "SELECT best_extension FROM mime WHERE mime_id = $1", std::move( extension_bindings ) )
				.rows< std::optional< std::string > >()
		};
		if ( !extensions.empty() )
		{
			const auto best { std::get< 0 >( extensions.row( 0 ) ) };
			if ( best && !best->empty() && *best != fileExtension( path ) )
			{
				const auto renamed { root / filesystem::getClusterRelativePath( hash, *best ) };
				auto moved { moveWithoutReplacing( path, renamed ) };
				if ( !moved ) co_return std::unexpected( std::move( moved.error() ) );
				path = renamed;
			}
		}
	}

	const auto extension { fileExtension( path ) };
	const auto times { filesystem::getFileTimes( path ) };
	if ( !times ) co_return std::unexpected( std::format( "Cannot stat {}", path.string() ) );

	db::DBBindings update_bindings {};
	update_bindings.bind( *record_id );
	update_bindings.bind( cluster_id );
	update_bindings.bind( static_cast< std::int64_t >( size ) );
	update_bindings.bind( mime_id );
	update_bindings.bind( extension );
	update_bindings.bind( filesystem::toMicroseconds( times->mtime ) );
	update_bindings.bind( filesystem::toMicroseconds( times->btime ) );
	co_await db
		.query(
			"INSERT INTO file_info (record_id, cluster_id, size, mime_id, extension, file_mtime, file_ctime, cluster_store_time) "
			"VALUES ($1, $2, $3, $4, $5, TIMESTAMP 'epoch' + $6::bigint * INTERVAL '1 microsecond', "
			"TIMESTAMP 'epoch' + $7::bigint * INTERVAL '1 microsecond', now()) "
			"ON CONFLICT (record_id) DO UPDATE SET cluster_id = $2, size = $3, mime_id = $4, extension = $5, "
			"file_mtime = EXCLUDED.file_mtime, file_ctime = EXCLUDED.file_ctime, "
			"cluster_store_time = COALESCE(file_info.cluster_store_time, now())",
			std::move( update_bindings ) )
		.result();

	if ( mime_id && ( params.scan_metadata || params.rescan_metadata ) )
	{
		db::DBBindings metadata_bindings {};
		metadata_bindings.bind( *record_id );
		const auto metadata {
			co_await db.query( "SELECT 1 FROM metadata WHERE record_id = $1", std::move( metadata_bindings ) )
				.rows< std::int32_t >()
		};
		if ( metadata.empty() || params.rescan_metadata )
		{
			auto parsed { co_await parseAndStoreRecordMetadata( db, *record_id ) };
			if ( !parsed ) co_return std::unexpected( responseText( parsed.error() ) );
		}
	}

	co_return {};
}

void recordFailure( ScanTotals& totals, const std::filesystem::path& path, const std::string& message )
{
	++totals.failed_count;
	if ( totals.failures.size() < 1000 )
	{
		Json::Value failure;
		failure[ "path" ] = path.string();
		failure[ "error" ] = message;
		totals.failures.append( std::move( failure ) );
	}
	log::warn( "Cluster scan failed for {}: {}", path.string(), message );
}

[[nodiscard]] runtime::Task< ScanOutcome > scanOne(
	db::DBContext& db,
	const ClusterID cluster_id,
	const std::filesystem::path& root,
	std::filesystem::path path,
	const std::uint64_t size,
	const ClusterScanParams& params )
{
	try
	{
		auto result { co_await scanFile( db, cluster_id, root, path, size, params ) };
		co_return ScanOutcome { std::move( path ), size, std::move( result ) };
	}
	catch ( const std::exception& error )
	{
		co_return ScanOutcome { std::move( path ), size, std::unexpected( std::string { error.what() } ) };
	}
}

[[nodiscard]] runtime::Task< bool > flushScanBatch(
	std::vector< runtime::Task< ScanOutcome > >& pending,
	ScanTotals& totals,
	const bool stop_on_fail )
{
	if ( pending.empty() ) co_return false;

	const auto outcomes { co_await folly::coro::collectAllRange( std::move( pending ) ) };
	pending.clear();
	bool stopped { false };
	for ( const auto& outcome : outcomes )
	{
		++totals.file_count;
		totals.byte_size += outcome.size;
		if ( outcome.result ) continue;
		recordFailure( totals, outcome.path, outcome.result.error() );
		stopped = stop_on_fail;
	}
	co_return stopped;
}

[[nodiscard]] runtime::Task< ScanTotals > scanCluster(
	db::DBContext& db,
	const ClusterID cluster_id,
	const std::filesystem::path& root,
	const ClusterScanParams& params )
{
	ScanTotals totals {};
	std::error_code error {};
	if ( !std::filesystem::is_directory( root, error ) || error )
	{
		recordFailure(
			totals,
			root,
			error ? std::format( "Cannot read cluster directory: {}", error.message() ) :
					"Cluster root is not a directory" );
		co_return totals;
	}

	auto preflight { co_await preflightMissingFiles( db, cluster_id, root, params, totals ) };
	if ( !preflight )
	{
		recordFailure( totals, root, preflight.error() );
		if ( params.stop_on_fail ) co_return totals;
	}
	else if ( *preflight )
		co_return totals;

	std::vector< runtime::Task< ScanOutcome > > pending {};
	pending.reserve( params.concurrency );
	for ( std::filesystem::directory_iterator folders { root, error }, end; folders != end; folders.increment( error ) )
	{
		if ( error ) break;
		const auto& folder { *folders };
		if ( folder.path().filename() == "bad" || !folder.is_directory( error ) || folder.is_symlink( error ) )
			continue;
		if ( error ) break;

		std::error_code child_error {};
		for ( std::filesystem::directory_iterator files { folder.path(), child_error }, file_end; files != file_end;
		      files.increment( child_error ) )
		{
			if ( child_error ) break;
			const auto& entry { *files };
			if ( entry.path().extension() == ".thumbnail" || !entry.is_regular_file( child_error )
			     || entry.is_symlink( child_error ) )
				continue;
			if ( child_error ) break;

			const auto size { entry.file_size( child_error ) };
			if ( child_error )
			{
				recordFailure( totals, entry.path(), child_error.message() );
				child_error.clear();
				if ( params.stop_on_fail )
				{
					co_await flushScanBatch( pending, totals, false );
					co_return totals;
				}
				continue;
			}

			pending.push_back( scanOne( db, cluster_id, root, entry.path(), size, params ) );
			if ( pending.size() >= params.concurrency )
			{
				const bool stopped { co_await flushScanBatch( pending, totals, params.stop_on_fail ) };
				if ( stopped ) co_return totals;
			}
		}
		if ( child_error )
		{
			recordFailure( totals, folder.path(), child_error.message() );
			if ( params.stop_on_fail )
			{
				co_await flushScanBatch( pending, totals, false );
				co_return totals;
			}
		}
	}
	if ( error ) recordFailure( totals, root, error.message() );
	co_await flushScanBatch( pending, totals, false );

	// Enumeration totals include rejected files; file_info triggers maintain database usage.
	co_return totals;
}

JobTask scanClusterJob(
	const ClusterID cluster_id,
	std::filesystem::path cluster_path,
	const ClusterScanParams params,
	db::DBContext& db )
{
	const auto executor { getJobRuntime().executor() };
	const auto totals {
		co_await folly::coro::co_withExecutor( executor, scanCluster( db, cluster_id, cluster_path, params ) )
	};

	Json::Value result;
	result[ "cluster_id" ] = cluster_id;
	result[ "scanned_count" ] = static_cast< Json::UInt64 >( totals.file_count );
	result[ "scanned_bytes" ] = static_cast< Json::UInt64 >( totals.byte_size );
	result[ "failed_count" ] = static_cast< Json::UInt64 >( totals.failed_count );
	result[ "failures" ] = totals.failures;
	co_await setJobResponse( std::move( result ) );
}

} // namespace idhan::httpapi
