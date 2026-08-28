#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "Config.hpp"
#include "MimeIDs.hpp"
#include "api/ClusterAPI.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "core/files/mime.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/size.hpp"
#include "filesystem/filesystem.hpp"
#include "filesystem/io/IOUring.hpp"
#include "fixme.hpp"
#include "hyapi/helpers.hpp"
#include "jobs/JobContext.hpp"
#include "logging/log.hpp"
#include "metadata/metadata.hpp"
#include "mime/identifyMime.hpp"
#include "modules/CallInput.hpp"
#include "records/records.hpp"
#include "threading/ExpectedTask.hpp"
#include "trantor/net/EventLoopThread.h"
#include "trantor/net/EventLoopThreadPool.h"

namespace idhan::api
{
struct ScanParams
{
	bool read_only { true };
	bool scan_mime { true };
	bool rescan_mime { false };
	bool scan_metadata { true };
	bool rescan_metadata { false };
	bool stop_on_fail { false };
	bool adopt_orphans { false };
	bool track_missing_files { false };
	bool fix_extensions { false };
	bool force_readonly { false };
	bool verify_hash { false };

	std::size_t concurrency { 4 };

	ScanParams() = default;
};

static ScanParams extractScanParams( const drogon::HttpRequestPtr& request )
{
	ScanParams p {};
	p.read_only = true;
	p.force_readonly = request->getOptionalParameter< bool >( "readonly" ).value_or( false );
	p.verify_hash = request->getOptionalParameter< bool >( "verify_hash" ).value_or( false );

	p.adopt_orphans = request->getOptionalParameter< bool >( "adopt_orphans" ).value_or( false );

	p.scan_mime = request->getOptionalParameter< bool >( "scan_mime" ).value_or( true );
	p.rescan_mime = request->getOptionalParameter< bool >( "rescan_mime" ).value_or( false );

	p.scan_metadata = request->getOptionalParameter< bool >( "scan_metadata" ).value_or( true );
	p.rescan_metadata = request->getOptionalParameter< bool >( "rescan_metadata" ).value_or( false );

	p.stop_on_fail = request->getOptionalParameter< bool >( "stop_on_fail" ).value_or( false );

	p.track_missing_files = request->getOptionalParameter< bool >( "remove_missing_files" ).value_or( false );
	p.fix_extensions = request->getOptionalParameter< bool >( "fix_extensions" ).value_or( false );

	p.scan_metadata |= p.adopt_orphans;
	p.scan_mime |= p.scan_metadata;

	constexpr std::size_t max_concurrency { 64 };
	const auto configured_concurrency {
		config::getSilentDefault< std::size_t >( "cluster", "scan_concurrency", std::size_t { 4 } )
	};
	p.concurrency = std::clamp(
		request->getOptionalParameter< std::size_t >( "concurrency" ).value_or( configured_concurrency ),
		std::size_t { 1 },
		max_concurrency );

	return p;
}

class ScanContext
{
	std::filesystem::path m_path;
	std::size_t m_size;

	ScanParams m_params {};
	MimeID m_mime_id { mime_ids::INVALID };
	SHA256 m_sha256 {};
	std::filesystem::path m_bad_dir {};

	static constexpr auto INVALID_RECORD { std::numeric_limits< RecordID >::max() };
	RecordID m_record_id { INVALID_RECORD };

	ClusterID m_cluster_id;
	std::filesystem::path m_cluster_path;

	[[nodiscard]] ExpectedTask< SHA256 > checkSHA256() const;
	[[nodiscard]] ExpectedTask< RecordID > checkRecord( DbClientPtr db );
	//! Returns true if the duplicate file at m_path was deleted (record already stored correctly
	//! in found_cluster_id). The caller must not touch m_path any further in that case.
	[[nodiscard]] ExpectedTask< bool > cleanupDoubleClusters( ClusterID found_cluster_id, DbClientPtr db );
	[[nodiscard]] Task<> updateFileModifiedTime( DbClientPtr db );
	[[nodiscard]] ExpectedTask< void > checkCluster( DbClientPtr db );
	[[nodiscard]] Task< bool > hasMime( DbClientPtr db );
	[[nodiscard]] ExpectedTask< void > scanMime( DbClientPtr db );
	[[nodiscard]] ExpectedTask< void > scanMetadata( DbClientPtr db );
	[[nodiscard]] ExpectedTask< void > checkExtension( DbClientPtr db );

  public:

	ScanContext(
		const std::filesystem::path& file_path,
		const std::size_t file_size,
		const ClusterID cluster_id,
		std::filesystem::path cluster_path,
		const ScanParams& params ) :
	  m_path( file_path ),
	  m_size( file_size ),
	  m_params( params ),
	  m_cluster_id( cluster_id ),
	  m_cluster_path( std::move( cluster_path ) )
	{}

	//! The scan may have moved the file within the cluster.
	[[nodiscard]] const std::filesystem::path& path() const { return m_path; }

	ExpectedTask< void > scan( std::filesystem::path bad_dir, DbClientPtr db );
};

static std::size_t countScanableFiles( const std::filesystem::path& directory )
{
	std::size_t count = 0;
	try
	{
		for ( const auto& entry : std::filesystem::directory_iterator( directory ) )
		{
			if ( !entry.is_regular_file() )
			{
				if ( entry.is_symlink() ) log::warn( "Skipping inaccessible symlink: {}", entry.path().string() );
				continue;
			}
			if ( entry.path().extension() == ".thumbnail" ) continue;
			++count;
		}
	}
	catch ( const std::filesystem::filesystem_error& e )
	{
		log::warn( "Cannot access directory {}: {}", directory.string(), e.what() );
	}
	return count;
}

struct FolderScanTotals
{
	std::size_t byte_size { 0 };
	std::size_t file_count { 0 };
};

//! Checks every database-assigned file before directory enumeration can hand anything to a worker.
static ExpectedTask< void > preflightExpectedFiles(
	const ClusterID cluster_id,
	const std::filesystem::path& cluster_path,
	const ScanParams& scan_params,
	DbClientPtr db )
{
	if ( !scan_params.track_missing_files ) co_return {};

	std::error_code root_error {};
	const auto root_status { std::filesystem::status( cluster_path, root_error ) };

	if ( root_error )
	{
		co_return std::unexpected( createInternalError(
			"Cannot access cluster {} root {}: {}", cluster_id, cluster_path.string(), root_error.message() ) );
	}

	if ( !std::filesystem::is_directory( root_status ) )
	{
		co_return std::unexpected(
			createInternalError( "Cluster {} root is not a directory: {}", cluster_id, cluster_path.string() ) );
	}

	const auto rows { co_await db->execSqlCoro(
		R"(SELECT fi.record_id, r.sha256, COALESCE(fi.extension, m.best_extension, '') AS extension
		     FROM file_info fi
		     JOIN records r USING (record_id)
		     LEFT JOIN mime m USING (mime_id)
		    WHERE fi.cluster_id = $1
		    ORDER BY fi.record_id)",
		cluster_id ) };

	for ( const auto& row : rows )
	{
		const auto record_id { row[ "record_id" ].as< RecordID >() };
		const auto sha256 { SHA256::fromPgCol( row[ "sha256" ] ) };
		const auto extension { row[ "extension" ].as< std::string >() };
		const auto relative_path { filesystem::getClusterRelativePath( sha256, extension ).lexically_normal() };

		if ( relative_path.empty() || relative_path.is_absolute() || *relative_path.begin() == ".." )
		{
			co_return std::unexpected( createInternalError(
				"Unsafe expected path for cluster {} record {}: {}", cluster_id, record_id, relative_path.string() ) );
		}

		const auto expected_path { cluster_path / relative_path };

		std::error_code status_error {};
		auto path_status { std::filesystem::status( expected_path, status_error ) };

		if ( status_error == std::errc::no_such_file_or_directory )
		{
			status_error.clear();
			path_status = std::filesystem::file_status { std::filesystem::file_type::not_found };
		}

		if ( status_error )
		{
			co_return std::unexpected( createInternalError(
				"Cannot inspect expected path for cluster {} record {} at {}: {}",
				cluster_id,
				record_id,
				expected_path.string(),
				status_error.message() ) );
		}

		if ( std::filesystem::is_regular_file( path_status ) ) continue;

		co_await filesystem::reportMissingFile( record_id, expected_path, db );

		if ( scan_params.stop_on_fail )
		{
			co_return std::unexpected( createInternalError(
				"Expected file for cluster {} record {} is missing at {}",
				cluster_id,
				record_id,
				expected_path.string() ) );
		}
	}

	co_return {};
}

//! Hands single files out to the scan workers, walking one folder at a time.
class ScanWorkQueue
{
	std::vector< std::filesystem::path > m_folders;
	std::size_t m_folder_index { 0 };
	std::optional< std::filesystem::directory_iterator > m_iterator {};
	std::mutex m_mutex {};
	std::atomic< bool > m_stopped { false };

  public:

	explicit ScanWorkQueue( std::vector< std::filesystem::path > folders ) : m_folders( std::move( folders ) ) {}

	void stop() { m_stopped.store( true, std::memory_order_release ); }

	[[nodiscard]] std::optional< std::filesystem::path > next();
};

std::optional< std::filesystem::path > ScanWorkQueue::next()
{
	if ( m_stopped.load( std::memory_order_acquire ) ) return std::nullopt;

	const std::lock_guard lock { m_mutex };

	while ( true )
	{
		if ( !m_iterator )
		{
			if ( m_folder_index >= m_folders.size() ) return std::nullopt;

			const auto& folder { m_folders[ m_folder_index++ ] };

			std::error_code open_error {};
			std::filesystem::directory_iterator iterator { folder, open_error };

			if ( open_error )
			{
				log::warn( "Cannot read directory {}: {}", folder.string(), open_error.message() );
				continue;
			}

			log::info( "Scanning folder {}", folder.string() );
			m_iterator = std::move( iterator );
		}

		auto& iterator { *m_iterator };

		if ( iterator == std::filesystem::directory_iterator {} )
		{
			m_iterator.reset();
			continue;
		}

		const auto entry { *iterator };

		std::error_code advance_error {};
		iterator.increment( advance_error );
		if ( advance_error )
		{
			log::warn( "Stopping directory walk early: {}", advance_error.message() );
			m_iterator.reset();
		}

		const auto& file_path { entry.path() };

		std::error_code type_error {};
		if ( !entry.is_regular_file( type_error ) || type_error )
		{
			if ( entry.is_symlink() )
			{
				log::warn( "Broken symlink detected (target inaccessible): {}", file_path.string() );
			}
			else
			{
				log::warn( "Skipping non-regular file: {}", file_path.string() );
			}
			continue;
		}

		if ( file_path.extension() == ".thumbnail" )
		{
			log::trace( "Skipping thumbnail file: {}", file_path.string() );
			continue;
		}

		return file_path;
	}
}

//! Held in a vector until when_all starts it, so all state has to arrive as a parameter.
ExpectedTask< FolderScanTotals > scanWorker(
	ScanWorkQueue* queue,
	const ScanParams* scan_params,
	const ClusterID cluster_id,
	const std::filesystem::path* cluster_path,
	const std::size_t total_files,
	std::atomic< std::size_t >* processed_files )
{
	const std::filesystem::path bad_dir { *cluster_path / "bad" };
	auto db { drogon::app().getDbClient() };

	FolderScanTotals totals {};

	while ( const auto file_path = queue->next() )
	{
		std::error_code size_error {};
		const auto initial_size { std::filesystem::file_size( *file_path, size_error ) };

		if ( size_error )
		{
			log::warn( "Cannot stat file {}: {}", file_path->string(), size_error.message() );
			processed_files->fetch_add( 1, std::memory_order_relaxed );
			continue;
		}

		ScanContext ctx { *file_path, initial_size, cluster_id, *cluster_path, *scan_params };

		const auto file_result { co_await ctx.scan( bad_dir, db ) };

		const auto processed { processed_files->fetch_add( 1, std::memory_order_relaxed ) + 1 };
		log::trace( "Scan progress: {}/{}", processed, total_files );

		std::error_code final_size_error {};
		const auto final_size { std::filesystem::file_size( ctx.path(), final_size_error ) };
		if ( !final_size_error )
		{
			totals.byte_size += final_size;
			++totals.file_count;
		}

		if ( scan_params->stop_on_fail && !file_result )
		{
			queue->stop();
			co_return std::unexpected( file_result.error() );
		}
	}

	co_return totals;
}

ExpectedTask< void > scanCluster(
	const ClusterID cluster_id,
	const std::filesystem::path cluster_path,
	const ScanParams& scan_params )
{
	log::info( "Starting scan of cluster {} at path {}", cluster_id, cluster_path.string() );

	const auto db { drogon::app().getDbClient() };
	const auto preflight_result { co_await preflightExpectedFiles( cluster_id, cluster_path, scan_params, db ) };
	return_unexpected_error( preflight_result );

	const auto bad_dir { cluster_path / "bad" };

	std::vector< std::filesystem::path > folders {};
	std::size_t total_files { 0 };
	try
	{
		for ( const auto& folder : std::filesystem::directory_iterator( cluster_path ) )
		{
			if ( !folder.is_directory() )
			{
				log::trace( "Skipping non-directory entry: {}", folder.path().string() );
				continue;
			}

			if ( folder.path() == bad_dir )
			{
				log::trace( "Skipping bad directory: {}", bad_dir.string() );
				continue;
			}

			folders.emplace_back( folder.path() );
			total_files += countScanableFiles( folder.path() );
		}
	}
	catch ( const std::filesystem::filesystem_error& e )
	{
		log::warn( "Cannot enumerate directories in cluster path {}: {}", cluster_path.string(), e.what() );
		co_return std::unexpected(
			createInternalError( "Cannot list cluster directory {}: {}", cluster_path.string(), e.what() ) );
	}

	log::debug( "Estimated files to scan: {}", total_files );

	std::atomic< std::size_t > processed_files { 0 };
	ScanWorkQueue queue { std::move( folders ) };

	const auto worker_count { std::min( scan_params.concurrency, std::max( total_files, std::size_t { 1 } ) ) };
	log::info( "Scanning cluster {} with {} workers", cluster_id, worker_count );

	std::vector< ExpectedTask< FolderScanTotals > > workers {};
	workers.reserve( worker_count );
	for ( std::size_t i = 0; i < worker_count; ++i )
		workers.emplace_back(
			scanWorker( &queue, &scan_params, cluster_id, &cluster_path, total_files, &processed_files ) );

	const auto worker_results { co_await drogon::when_all( std::move( workers ) ) };

	std::size_t cluster_byte_size_total { 0 };
	std::size_t cluster_file_count_total { 0 };
	drogon::HttpResponsePtr worker_error {};

	for ( const auto& worker_result : worker_results )
	{
		if ( !worker_result )
		{
			if ( !worker_error ) worker_error = worker_result.error();
			continue;
		}

		cluster_byte_size_total += worker_result->byte_size;
		cluster_file_count_total += worker_result->file_count;
	}

	if ( worker_error )
	{
		log::warn( "Scan of cluster {} stopped early on a file failure", cluster_id );
		co_return std::unexpected( worker_error );
	}

	co_await db->execSqlCoro(
		"UPDATE file_clusters SET size_used = $1, file_count = $2 WHERE cluster_id = $3",
		cluster_byte_size_total,
		static_cast< std::int32_t >( cluster_file_count_total ),
		cluster_id );

	log::info( "Completed scan of cluster {}", cluster_id );
	co_return {};
}

JobTask scanJob( const ClusterID cluster_id, const std::filesystem::path cluster_path, const ScanParams scan_params )
{
	log::info( "Scan job started for cluster {} at path {}", cluster_id, cluster_path.string() );

	log::trace(
		"Scan params: read_only={}, scan_mime={}, rescan_mime={}, scan_metadata={}, rescan_metadata={}, "
		"stop_on_fail={}, adopt_orphans={}, track_missing_files={}, fix_extensions={}, verify_hash={}, concurrency={}",
		scan_params.read_only,
		scan_params.scan_mime,
		scan_params.rescan_mime,
		scan_params.scan_metadata,
		scan_params.rescan_metadata,
		scan_params.stop_on_fail,
		scan_params.adopt_orphans,
		scan_params.track_missing_files,
		scan_params.fix_extensions,
		scan_params.verify_hash,
		scan_params.concurrency );

	drogon::HttpResponsePtr error_response;

	try
	{
		const auto cluster_result { co_await scanCluster( cluster_id, cluster_path, scan_params ) };

		if ( !cluster_result )
		{
			log::error( "Scan job failed for cluster {}: {}", cluster_id, cluster_result.error()->getBody() );
			co_await setJobResponse( cluster_result.error() );
			co_return;
		}

		log::info( "Scan job completed for cluster {}", cluster_id );
		co_await setJobResponse( Json::Value( "completed" ) );
		co_return;
	}
	catch ( const std::exception& e )
	{
		log::error( "Scan job failed for cluster {} with unexpected exception: {}", cluster_id, e.what() );
		error_response = createInternalError( "Scan job failed for cluster {}: {}", cluster_id, e.what() );
	}

	co_await setJobResponse( error_response );
	co_return;
}

ResponseTask ClusterAPI::scan( const drogon::HttpRequestPtr request, const ClusterID cluster_id )
{
	const auto db { drogon::app().getDbClient() };
	auto scan_params { extractScanParams( request ) };

	const auto result {
		co_await db->execSqlCoro( "SELECT folder_path, read_only FROM file_clusters WHERE cluster_id = $1", cluster_id )
	};

	if ( result.empty() )
	{
		co_return createNotFound( "Cluster not found" );
	}

	scan_params.read_only = scan_params.force_readonly || result[ 0 ][ "read_only" ].as< bool >();

	const std::filesystem::path cluster_path { result[ 0 ][ "folder_path" ].as< std::string >() };

	const auto job_ctx {
		queueJob( scanJob( cluster_id, cluster_path, scan_params ), std::format( "Scanning cluster {}", cluster_id ) )
	};

	Json::Value response;
	response[ "job_id" ] = job_ctx->id();

	co_return drogon::HttpResponse::newHttpJsonResponse( response );
}

ExpectedTask< SHA256 > ScanContext::checkSHA256() const
{
	const auto file_stem { m_path.stem().string() };
	std::shared_ptr< FileIOUring > uring { std::make_shared< FileIOUring >( m_path ) };

	auto sha256_e { m_params.verify_hash ? co_await SHA256::hashCoro( uring ) : SHA256::fromHex( file_stem ) };

	if ( m_params.verify_hash )
	{
		log::trace( "Hashing file {} via io_uring (verify_hash=true)", m_path.string() );
	}
	else
	{
		log::trace( "Using filename stem {} as hash for {}", file_stem, m_path.string() );
	}

	if ( !sha256_e )
	{
		log::warn(
			"When attempting to get hash from {}, the hash from the filename was invalid or not a hash. Forcing hashing of file",
			m_path.string() );
		sha256_e = co_await SHA256::hashCoro( uring );
		log::trace( "Fallback hash completed for {}", m_path.string() );
	}

	if ( !sha256_e ) co_return std::unexpected( sha256_e.error() );

	const auto sha256_hex { sha256_e->hex() };
	log::trace( "Computed hash for {}: {}", m_path.string(), sha256_hex );

	if ( sha256_hex == file_stem )
	{
		log::trace( "Hash matches filename for {}", m_path.string() );
		co_return *sha256_e;
	}

	log::warn(
		"While scanning file at {} it was detected that the filename does not match the SHA256 {}",
		m_path.string(),
		sha256_hex );

	if ( m_params.read_only )
	{
		co_return std::unexpected( createInternalError(
			"When scanning file at {} it was detected that the filename does not match the sha256 {}. "
			"Because the cluster is in readonly mode, this could not be automatically fixed",
			m_path.string(),
			sha256_hex ) );
	}

	try
	{
		std::filesystem::create_directories( m_bad_dir );
		const auto new_path { m_bad_dir / m_path.filename() };
		std::filesystem::rename( m_path, new_path );

		co_return std::unexpected( createInternalError(
			"When scanning file at {} it was detected that the filename does not match the sha256 {}. "
			"The file has been moved to {}",
			m_path.string(),
			sha256_hex,
			new_path.string() ) );
	}
	catch ( const std::exception& e )
	{
		co_return std::unexpected( createInternalError(
			"When scanning file at {} it was detected that the filename does not match the sha256 {}. "
			"There was an error that prevented this from being fixed: {}",
			m_path.string(),
			sha256_hex,
			e.what() ) );
	}
}

ExpectedTask< RecordID > ScanContext::checkRecord( drogon::orm::DbClientPtr db )
{
	const auto existing { co_await idhan::helpers::findRecord( m_sha256, db ) };

	if ( !existing && m_params.adopt_orphans )
	{
		log::trace( "Hashing file at {} because it's never been seen before to verify the filename", m_path.string() );
		m_params.verify_hash = true;
		const auto verified_hash_result { co_await checkSHA256() };
		return_unexpected_error( verified_hash_result );
		m_sha256 = *verified_hash_result;

		const auto new_record_id { co_await idhan::helpers::createRecord( m_sha256, db ) };
		return_unexpected_error( new_record_id );

		log::debug( "Created new record {} for orphan file {}", *new_record_id, m_path.string() );
		co_return *new_record_id;
	}

	if ( !existing )
	{
		log::trace( "No existing record found for {} and adopt_orphans is false", m_path.string() );
		co_return std::unexpected( createInternalError(
			"When scanning cluster {} file {} was not found as a existing record and scan was not set to adopt orphans",
			m_cluster_id,
			m_path.string() ) );
	}

	const auto found_record_id { *existing };
	log::trace( "Found existing record {} for file {}", found_record_id, m_path.string() );
	co_return found_record_id;
}

ExpectedTask< bool > ScanContext::cleanupDoubleClusters( const ClusterID found_cluster_id, drogon::orm::DbClientPtr db )
{
	log::trace(
		"cleanupDoubleClusters: record_id={}, found_cluster_id={}, current_cluster_id={}",
		m_record_id,
		found_cluster_id,
		m_cluster_id );
	if ( found_cluster_id == 0 ) co_return false;

	const auto found_state { co_await filesystem::validateFile( m_record_id, db ) };
	return_unexpected_error( found_state );

	if ( *found_state == filesystem::FileState::FileInvalidHash )
	{
		log::warn(
			"Record {} file in cluster {} does not match it's hash. Keeping the copy in cluster {} instead",
			m_record_id,
			found_cluster_id,
			m_cluster_id );
	}

	if ( *found_state == filesystem::FileState::FileValid )
	{
		log::warn(
			"Found identical file for record {} in both cluster {} and {}.",
			m_record_id,
			found_cluster_id,
			m_cluster_id );

		if ( !m_params.read_only )
		{
			log::warn(
				"Deleting record {} file from cluster {} because it exists in cluster {} already",
				m_record_id,
				m_cluster_id,
				found_cluster_id );

			std::filesystem::remove( m_path );
			co_return true;
		}

		co_return false;
	}

	if ( *found_state == filesystem::FileState::FileNotFound )
	{
		log::warn(
			"File {} was missing from it's expected cluster of {}. Setting the record as being stored in cluster {} instead",
			m_record_id,
			found_cluster_id,
			m_cluster_id );
	}

	co_await db->execSqlCoro( "UPDATE file_info SET cluster_id = $1 WHERE record_id = $2", m_cluster_id, m_record_id );

	co_return false;
}

Task<> ScanContext::updateFileModifiedTime( DbClientPtr db )
{
	const auto modified_time { filesystem::getLastWriteTime( m_path ) };
	const auto modified_time_us {
		std::chrono::duration_cast< std::chrono::microseconds >( modified_time.time_since_epoch() ).count()
	};

	log::trace( "mtime is {}", format_ns::format( "{:%F %T}", modified_time ) );

	co_await db->execSqlCoro(
		"UPDATE file_info SET modified_time = TIMESTAMP 'epoch' + $1::bigint * INTERVAL '1 microsecond' WHERE record_id = $2",
		modified_time_us,
		m_record_id );
}

ExpectedTask< void > ScanContext::checkCluster( drogon::orm::DbClientPtr db )
{
	log::trace( "Verifying that record {} is in the correct cluster", m_record_id );
	FGL_ASSERT( m_record_id != INVALID_RECORD, "Invalid record" );
	const auto file_info { co_await db->execSqlCoro(
		"SELECT cluster_id, modified_time, cluster_store_time FROM file_info WHERE record_id = $1", m_record_id ) };

	if ( file_info.empty() )
	{
		log::trace( "No file_info entry found for record {} (first scan or new adopt)", m_record_id );
		co_return {};
	}

	if ( file_info[ 0 ][ "cluster_store_time" ].isNull() )
	{
		log::trace( "cluster_store_time is null for record {}, updating", m_record_id );
		co_await db->execSqlCoro(
			"UPDATE file_info SET cluster_store_time = now() WHERE record_id = $1 AND cluster_store_time IS NULL",
			m_record_id );
	}

	if ( file_info[ 0 ][ "modified_time" ].isNull() )
	{
		log::trace( "modified_time is null for record {}, updating", m_record_id );
		co_await updateFileModifiedTime( db );
	}

	const auto found_cluster_id { file_info[ 0 ][ 0 ].as< ClusterID >() };
	FGL_ASSERT( m_cluster_id != 0, "Cluster should not be zero" );
	FGL_ASSERT( found_cluster_id != 0, "Cluster should not be zero" );
	const bool clusters_match { found_cluster_id == m_cluster_id };
	log::trace(
		"Record {} cluster check: stored={}, current={}, match={}",
		m_record_id,
		found_cluster_id,
		m_cluster_id,
		clusters_match );

	if ( !clusters_match )
	{
		const auto cleanup_result { co_await cleanupDoubleClusters( found_cluster_id, db ) };
		return_unexpected_error( cleanup_result );

		// m_path was deleted as a duplicate; nothing left at this path to path-check or move
		if ( cleanup_result.value() ) co_return {};
	}

	const auto current_parent { m_path.parent_path() };
	const auto expected_cluster_subfolder { filesystem::getFileFolder( m_sha256 ) };
	const auto expected_parent_path { m_cluster_path / expected_cluster_subfolder };

	log::trace(
		"Path check: current_parent={}, expected_parent={}", current_parent.string(), expected_parent_path.string() );

	if ( current_parent != expected_parent_path )
	{
		log::warn(
			"Expected file {} to be in path {} but was found in {} instead (Record {})",
			m_path.filename().string(),
			expected_parent_path.string(),
			current_parent.string(),
			m_record_id );

		if ( !m_params.read_only )
		{
			const auto new_path { expected_parent_path / m_path.filename() };
			log::info( "Moving file {} to {} because it was in the wrong cluster", m_path.string(), new_path.string() );

			std::filesystem::create_directories( expected_parent_path );

			std::filesystem::rename( m_path, new_path );

			m_path = new_path;
			log::trace( "File moved to new path: {}", m_path.string() );
		}
	}
	else
	{
		log::trace( "Record {} file is in the correct directory", m_record_id );
	}

	log::trace( "Record {} passed cluster check", m_record_id );

	co_return {};
}

Task< bool > ScanContext::hasMime( DbClientPtr db )
{
	log::trace( "Checking if record {} has a mime", m_record_id );
	const auto current_mime { co_await mime::lookupRecordMimeID( m_record_id, db ) };

	if ( current_mime.mime_id )
	{
		m_mime_id = *current_mime.mime_id;
		log::trace( "Found that record {} has mime id {}", m_record_id, m_mime_id );
		co_return true;
	}

	log::trace( "Record {} does not have a mime", m_record_id );
	co_return false;
}

ExpectedTask< void > ScanContext::scanMime( DbClientPtr db )
{
	FGL_ASSERT( m_record_id != INVALID_RECORD, "Invalid record" );
	if ( !m_params.rescan_mime && co_await hasMime( db ) )
	{
		log::trace( "Skipping metadata scan because it already had metadata and rescan_mime was set to false" );
		co_return {};
	}

	log::trace( "Starting mime scan for {} (Record {})", m_path.filename().string(), m_record_id );
	const auto mime_id { co_await mime::identifyMimeForPath( m_path ) };
	log::trace(
		"Mime scan completed for {} (Record {}), result: {}",
		m_path.filename().string(),
		m_record_id, mime_id );

	const auto modified_time { filesystem::getLastWriteTime( m_path ) };
	const auto modified_time_us {
		std::chrono::duration_cast< std::chrono::microseconds >( modified_time.time_since_epoch() ).count()
	};
	log::trace(
		"File mtime retrieved for {} (Record {}): {}",
		m_path.filename().string(),
		m_record_id,
		format_ns::format( "{:%F %T}", modified_time ) );

	if ( mime_id == mime_ids::UNKNOWN )
	{
		std::string extension_str { m_path.extension().string() };

		if ( extension_str.starts_with( "." ) ) extension_str = extension_str.substr( 1 );

		log::warn(
			"During a cluster scan file {} failed to be detected by any mime parsers; It has been added despite this and has an extension override of \'{}\' (Record {})",
			m_path.filename().string(),
			extension_str,
			m_record_id );

		log::trace( "Inserting file_info for {} with extension override and NULL mime_id", m_record_id );
		co_await db->execSqlCoro(
			"INSERT INTO file_info (record_id, size, extension, modified_time, cluster_id, cluster_store_time) VALUES ($1, $2, $3, TIMESTAMP 'epoch' + $4::bigint * INTERVAL '1 microsecond', $5, now()) ON CONFLICT (record_id) DO UPDATE SET extension = $3, mime_id = NULL, modified_time = EXCLUDED.modified_time, cluster_store_time = COALESCE(file_info.cluster_store_time, now())",
			m_record_id,
			m_size,
			extension_str,
			modified_time_us,
			m_cluster_id );

		co_return {};
	}

	log::trace( "Detected mime id {} for file {} (Record {})", mime_id, m_path.filename().string(), m_record_id );

	m_mime_id = mime_id;
	const auto specialized_mime_id { m_mime_id };

	log::trace( "Upserting file_info for record {} with mime_id={}", m_record_id, specialized_mime_id );
	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, size, mime_id, modified_time, cluster_id, cluster_store_time) VALUES ($1, $2, $3, TIMESTAMP 'epoch' + $4::bigint * INTERVAL '1 microsecond', $5, now()) ON CONFLICT (record_id) DO UPDATE SET mime_id = $3, modified_time = EXCLUDED.modified_time, cluster_store_time = COALESCE(file_info.cluster_store_time, now())",
		m_record_id,
		m_size,
		specialized_mime_id,
		modified_time_us,
		m_cluster_id );

	{
		const auto mime_info {
			co_await db->execSqlCoro( "SELECT 1 FROM mime WHERE mime_id = $1", specialized_mime_id )
		};
		if ( mime_info.empty() )
			co_return std::unexpected(
				createInternalError( "When selecting mime id {} the DB returned zero rows", specialized_mime_id ) );
	}

	co_return {};
}

ExpectedTask< void > ScanContext::scanMetadata( DbClientPtr db )
{
	if ( m_mime_id == mime_ids::INVALID )
	{
		co_return std::unexpected( createInternalError(
			"Unable to determine metadata parser for {} (Record {}): No mime found",
			m_path.filename().string(),
			m_record_id ) );
	}

	if ( !m_params.rescan_metadata )
	{
		const auto current_metadata {
			co_await db->execSqlCoro( "SELECT 1 FROM metadata WHERE record_id = $1", m_record_id )
		};
		if ( !current_metadata.empty() )
		{
			log::trace(
				"Skipping metadata scan for {} because it's already been parsed and rescan_metadata was false",
				m_record_id );
			co_return {};
		}
	}

	co_await updateFileModifiedTime( db );

	auto input { modules::CallInput::forPath( m_path ) };
	if ( !input )
		co_return std::unexpected(
			createInternalError( "Failed to open file for record {}: {}", m_record_id, input.error() ) );

	co_return co_await metadata::parseAndUpdateRecordMetadata(
		m_record_id, m_mime_id, std::make_shared< const modules::CallInput >( std::move( *input ) ), db );
}

ExpectedTask< void > ScanContext::checkExtension( DbClientPtr db )
{
	log::trace( "Starting extension check for Record {} ({})", m_record_id, m_path.filename().string() );

	auto getExtensionFromMimeID = [ & ]() -> drogon::Task< std::string >
	{
		log::trace( "Looking up extension from mime id {}", m_mime_id );
		const auto mime_info { co_await mime::findMime( m_mime_id, db ) };

		if ( !mime_info ) co_return std::string {};
		co_return mime_info->extension;
	};

	auto getExtensionFromRecord = [ & ]() -> drogon::Task< std::string >
	{
		log::trace( "Looking up extension from record {} file_info", m_record_id );
		const auto result { co_await db->execSqlCoro(
			"SELECT COALESCE(best_extension, extension) FROM file_info LEFT JOIN mime USING (mime_id) WHERE record_id = $1",
			m_record_id ) };

		if ( result.empty() ) co_return std::string {};
		co_return result[ 0 ][ 0 ].as< std::string >();
	};

	const auto expected_extension {
		m_mime_id == mime_ids::INVALID ? co_await getExtensionFromRecord() : co_await getExtensionFromMimeID()
	};

	log::trace( "Expected extension for Record {}: '{}'", m_record_id, expected_extension );

	if ( expected_extension.empty() )
	{
		log::trace( "Could not determine expected extension for Record {}", m_record_id );
		co_return {};
	}

	std::string file_extension { m_path.extension().string() };
	if ( file_extension.starts_with( "." ) ) file_extension = file_extension.substr( 1 );

	if ( expected_extension != file_extension )
	{
		log::warn(
			"When scanning {} it was detected that the extension did not match it's mime, Expected {} got {} (Record {})",
			m_path.filename().string(),
			expected_extension,
			file_extension,
			m_record_id );

		if ( !m_params.read_only && m_params.fix_extensions )
		{
			auto new_path { m_path };
			new_path.replace_extension( format_ns::format( ".{}", expected_extension ) );
			std::filesystem::rename( m_path, new_path );
			log::info( "Renamed file {} to {} due to extension mismatch", m_path.string(), new_path.string() );
			m_path = std::move( new_path );
		}
	}
	else
	{
		log::trace(
			"Record {} passed extension check. File extension '{}' matches expected '{}'",
			m_record_id,
			file_extension,
			expected_extension );
	}

	co_return {};
}

ExpectedTask< void > ScanContext::scan( const std::filesystem::path bad_dir, drogon::orm::DbClientPtr db )
{
	log::info( "Scanning file: {} (size: {})", m_path.string(), m_size );

	if ( m_size == 0 )
	{
		log::warn( "File {} has zero size, skipping", m_path.string() );
		co_return std::unexpected( createInternalError(
			"When scanning file: {} it was detected that it has a filesize of zero!", m_path.string() ) );
	}

	m_bad_dir = bad_dir;

	log::trace( "Step 1/6: Hashing file {}", m_path.string() );
	const auto sha256_result { co_await checkSHA256() };
	return_unexpected_error( sha256_result );
	m_sha256 = *sha256_result;
	log::trace( "Step 1/6: Hash complete for {}", m_path.string() );

	log::trace( "Step 2/6: Checking record for {}", m_path.string() );
	const auto record_result { co_await checkRecord( db ) };
	return_unexpected_error( record_result );
	m_record_id = *record_result;
	log::trace( "File {} resolved to record {}", m_path.string(), m_record_id );

	log::trace( "Step 3/6: Checking cluster assignment for record {}", m_record_id );
	const auto cluster_result { co_await checkCluster( db ) };
	return_unexpected_error( cluster_result );
	log::trace( "Step 3/6: Cluster check passed for record {}", m_record_id );

	log::trace( "Step 4/6: Checking mime for record {}", m_record_id );
	bool has_mime_info { co_await hasMime( db ) };

	if ( ( m_params.scan_mime && !has_mime_info ) || m_params.rescan_mime )
	{
		log::trace( "Running mime scan for record {} ({})", m_record_id, m_path.filename().string() );
		const auto mime_result { co_await scanMime( db ) };
		if ( !mime_result )
		{
			const auto msg { hyapi::helpers::extractHttpResponseErrorMessage( mime_result.error() ) };
			const auto error_msg { std::format(
				"Failed to process mime for {} (Record {}): {}", m_path.filename().string(), m_record_id, msg ) };
			log::warn( error_msg );
			co_return std::unexpected( createInternalError( error_msg ) );
		}
		has_mime_info = co_await hasMime( db );
	}
	log::trace( "Step 4/6: Mime check complete for record {} (has_mime={})", m_record_id, has_mime_info );

	if ( has_mime_info )
	{
		log::trace( "Step 5/6: Checking extension for Record {} ({})", m_record_id, m_path.filename().string() );
		const auto extenion_result { co_await checkExtension( db ) };
		return_unexpected_error( extenion_result );
		log::trace( "Step 5/6: Extension check passed for record {}", m_record_id );
	}
	else
	{
		log::trace( "Step 5/6: Skipping extension check (no mime info) for record {}", m_record_id );
	}

	if ( ( m_params.scan_metadata || m_params.rescan_metadata ) && has_mime_info )
	{
		log::trace( "Step 6/6: Scanning metadata for record {} ({})", m_record_id, m_path.filename().string() );
		const auto metadata_result { co_await scanMetadata( db ) };
		return_unexpected_error( metadata_result );
		log::trace( "Step 6/6: Metadata scan complete for record {}", m_record_id );
	}
	else
	{
		log::trace(
			"Step 6/6: Skipping metadata scan for record {} (scan_metadata={}, has_mime={})",
			m_record_id,
			m_params.scan_metadata,
			has_mime_info );
	}

	log::trace( "Finished scanning file {} (Record {})", m_path.string(), m_record_id );

	co_return {};
}
} // namespace idhan::api
