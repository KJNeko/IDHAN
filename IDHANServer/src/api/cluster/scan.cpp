#include <utility>

#include "Config.hpp"
#include "MetadataModule.hpp"
#include "api/ClusterAPI.hpp"
#include "api/helpers/ResponseCallback.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/size.hpp"
#include "filesystem/filesystem.hpp"
#include "filesystem/io/IOUring.hpp"
#include "fixme.hpp"
#include "hyapi/helpers.hpp"
#include "jobs/JobContext.hpp"
#include "logging/log.hpp"
#include "metadata/metadata.hpp"
#include "mime/FileInfo.hpp"
#include "mime/MimeDatabase.hpp"
#include "modules/RemoteModule.hpp"
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
	bool remove_missing_files { false };
	bool fix_extensions { false };
	bool force_readonly { false };
	bool verify_hash { false };

	ScanParams() = default;
};

// Extracts boolean parameters safely
static ScanParams extractScanParams( const drogon::HttpRequestPtr& request )
{
	ScanParams p {};
	p.read_only = true; // set to false when getting the cluster info only if it's not read only
	p.force_readonly = request->getOptionalParameter< bool >( "readonly" ).value_or( false );
	p.verify_hash = request->getOptionalParameter< bool >( "verify_hash" ).value_or( false );

	p.adopt_orphans = request->getOptionalParameter< bool >( "adopt_orphans" ).value_or( false );

	p.scan_mime = request->getOptionalParameter< bool >( "scan_mime" ).value_or( true );
	p.rescan_mime = request->getOptionalParameter< bool >( "rescan_mime" ).value_or( false );

	p.scan_metadata = request->getOptionalParameter< bool >( "scan_metadata" ).value_or( true );
	p.rescan_metadata = request->getOptionalParameter< bool >( "rescan_metadata" ).value_or( false );

	p.stop_on_fail = request->getOptionalParameter< bool >( "stop_on_fail" ).value_or( false );

	p.remove_missing_files = request->getOptionalParameter< bool >( "remove_missing_files" ).value_or( false );
	p.fix_extensions = request->getOptionalParameter< bool >( "fix_extensions" ).value_or( false );

	p.scan_metadata |= p.adopt_orphans; // orphans will need to be scanned for metadata
	p.scan_mime |= p.scan_metadata; // mime is needed for metadata
	// if read only then we need to recompute the hash because the file path can't be trusted anymore

	return p;
}

class ScanContext
{
	std::filesystem::path m_path;
	std::size_t m_size;

	ScanParams m_params {};
	std::string m_mime_name {};
	SHA256 m_sha256 {};
	std::filesystem::path m_bad_dir {};

	static constexpr auto INVALID_RECORD { std::numeric_limits< RecordID >::max() };
	RecordID m_record_id { INVALID_RECORD };

	ClusterID m_cluster_id;
	std::filesystem::path m_cluster_path;

	[[nodiscard]] ExpectedTask< SHA256 > checkSHA256() const;
	[[nodiscard]] ExpectedTask< RecordID > checkRecord( DbClientPtr db );
	//! Returns true if the duplicate file at m_path was deleted (record already stored correctly
	//! in found_cluster_id) -- the caller must not touch m_path any further in that case.
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
		const ClusterID cluster_id,
		std::filesystem::path cluster_path,
		const ScanParams& params ) :
	  m_path( file_path ),
	  m_size( std::filesystem::file_size( file_path ) ),
	  m_params( params ),
	  m_cluster_id( cluster_id ),
	  m_cluster_path( std::move( cluster_path ) )
	{}

	//! The file's current location. The scan may have moved it within the cluster.
	[[nodiscard]] const std::filesystem::path& path() const { return m_path; }

	ExpectedTask< void > scan( std::filesystem::path bad_dir, DbClientPtr db );
};

/**
 *
 * @param folder Folder to scan
 * @param scan_params Parameters for scanning
 * @param cluster_id Cluster ID this folder is in
 * @param cluster_path Path to the root of the cluster
 * @return Returns void if successful, an error if any files in the folder fail to scan, if scan_params.stop_on_fail is false then the scanFolder will complete successfully in all cases
 */
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

ExpectedTask< FolderScanTotals > scanFolder(
	const std::filesystem::path folder,
	const ScanParams& scan_params,
	const ClusterID cluster_id,
	const std::filesystem::path cluster_path,
	const std::size_t total_files,
	std::atomic< std::size_t >& processed_files )
{
	const std::filesystem::path bad_dir { cluster_path / "bad" };
	auto db { drogon::app().getDbClient() };

	FolderScanTotals totals {};
	try
	{
		for ( const auto& file : std::filesystem::directory_iterator( folder ) )
		{
			const auto& entry { file };

			const auto& file_path { entry.path() };

			if ( !entry.is_regular_file() )
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

			// ignore thumbnails
			if ( file_path.extension() == ".thumbnail" )
			{
				log::trace( "Skipping thumbnail file: {}", file_path.string() );
				continue;
			}

			ScanContext ctx { file_path, cluster_id, cluster_path, scan_params };

			const auto file_result { co_await ctx.scan( bad_dir, db ) };

			++processed_files;
			log::trace( "Scan progress: {}/{}", processed_files.load(), total_files );

			std::error_code size_error {};
			const auto final_size { std::filesystem::file_size( ctx.path(), size_error ) };
			if ( !size_error )
			{
				totals.byte_size += final_size;
				++totals.file_count;
			}

			if ( scan_params.stop_on_fail ) return_unexpected_error( file_result );
		}
	}
	catch ( const std::filesystem::filesystem_error& e )
	{
		log::warn( "Cannot read directory {}: {}", folder.string(), e.what() );
		co_return std::unexpected( createInternalError( "Failed to scan folder {}: {}", folder.string(), e.what() ) );
	}

	co_return totals;
}

ExpectedTask< void > scanCluster(
	const ClusterID cluster_id,
	const std::filesystem::path cluster_path,
	const ScanParams& scan_params )
{
	log::info( "Starting scan of cluster {} at path {}", cluster_id, cluster_path.string() );

	const auto bad_dir { cluster_path / "bad" };

	std::size_t total_files { 0 };
	try
	{
		for ( const auto& folder : std::filesystem::directory_iterator( cluster_path ) )
		{
			if ( !folder.is_directory() ) continue;
			if ( folder.path() == bad_dir ) continue;
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
	std::size_t cluster_byte_size_total { 0 };
	std::size_t cluster_file_count_total { 0 };

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

			log::info( "Scanning folder {} in cluster {}", folder.path().string(), cluster_id );

			const auto folder_result { co_await scanFolder(
				folder.path(), scan_params, cluster_id, cluster_path, total_files, processed_files ) };

			if ( !folder_result )
			{
				log::warn( "Folder {} scan failed in cluster {}", folder.path().string(), cluster_id );
				if ( scan_params.stop_on_fail ) return_unexpected_error( folder_result );
			}
			else
			{
				cluster_byte_size_total += folder_result->byte_size;
				cluster_file_count_total += folder_result->file_count;
			}
		}
	}
	catch ( const std::filesystem::filesystem_error& e )
	{
		log::warn( "Cannot enumerate directories in cluster path {}: {}", cluster_path.string(), e.what() );
		co_return std::unexpected(
			createInternalError( "Cannot list cluster directory {}: {}", cluster_path.string(), e.what() ) );
	}

	const auto db { drogon::app().getDbClient() };
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
		"stop_on_fail={}, adopt_orphans={}, fix_extensions={}, verify_hash={}",
		scan_params.read_only,
		scan_params.scan_mime,
		scan_params.rescan_mime,
		scan_params.scan_metadata,
		scan_params.rescan_metadata,
		scan_params.stop_on_fail,
		scan_params.adopt_orphans,
		scan_params.fix_extensions,
		scan_params.verify_hash );

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

	// Move the file to bad directory
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
	const auto search_result {
		co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", m_sha256.toVec() )
	};

	if ( search_result.empty() && m_params.adopt_orphans )
	{
		log::trace( "Hashing file at {} because it's never been seen before to verify the filename", m_path.string() );
		m_params.verify_hash = true;
		const auto verified_hash_result { co_await checkSHA256() };
		return_unexpected_error( verified_hash_result );
		m_sha256 = *verified_hash_result;

		const auto insert_result {
			co_await db->execSqlCoro( "INSERT INTO records (sha256) VALUES ($1) RETURNING record_id", m_sha256.toVec() )
		};

		if ( insert_result.empty() )
		{
			co_return std::unexpected( createInternalError( "Failed to create a record for hash {}", m_sha256.hex() ) );
		}

		const auto new_record_id { insert_result[ 0 ][ 0 ].as< RecordID >() };
		log::debug( "Created new record {} for orphan file {}", new_record_id, m_path.string() );
		co_return new_record_id;
	}
	else if ( search_result.empty() )
	{
		log::trace( "No existing record found for {} and adopt_orphans is false", m_path.string() );
		co_return std::unexpected( createInternalError(
			"When scanning cluster {} file {} was not found as a existing record and scan was not set to adopt orphans",
			m_cluster_id,
			m_path.string() ) );
	}

	const auto found_record_id { search_result[ 0 ][ 0 ].as< RecordID >() };
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

	if ( co_await filesystem::checkFileExists( m_record_id, db ) )
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

	log::warn(
		"File {} was missing from it's expected cluster of {}. Setting the record as being stored in cluster {} instead",
		m_record_id,
		found_cluster_id,
		m_cluster_id );

	co_await db->execSqlCoro( "UPDATE file_info SET cluster_id = $1 WHERE record_id = $2", m_cluster_id, m_record_id );

	co_return false;
}

Task<> ScanContext::updateFileModifiedTime( DbClientPtr db )
{
	const trantor::Date date { filesystem::getLastWriteTime( m_path ) };

	log::trace( "mtime is {}", date.toFormattedString( true ) );

	co_await db->execSqlCoro( "UPDATE file_info SET modified_time = $1 WHERE record_id = $2", date, m_record_id );
}

ExpectedTask< void > ScanContext::checkCluster( drogon::orm::DbClientPtr db )
{
	log::trace( "Verifying that record {} is in the correct cluster", m_record_id );
	FGL_ASSERT( m_record_id != INVALID_RECORD, "Invalid record" );
	const auto file_info {
		co_await db->execSqlCoro( "SELECT cluster_id, modified_time FROM file_info WHERE record_id = $1", m_record_id )
	};

	if ( file_info.empty() )
	{
		log::trace( "No file_info entry found for record {} (first scan or new adopt)", m_record_id );
		co_return {};
		// co_return co_await insertFileInfo( db );
	}

	if ( file_info[ 0 ][ "modified_time" ].isNull() )
	{
		log::trace( "modified_time is null for record {}, updating", m_record_id );
		co_await updateFileModifiedTime( db );
	}

	// we found a cluster, check if it's the one we are about to add too
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
		// handle the double count, which will check if the found cluster contains the file and delete it from this one
		// if found. Otherwise the record's cluster is set to the current cluster
		const auto cleanup_result { co_await cleanupDoubleClusters( found_cluster_id, db ) };
		return_unexpected_error( cleanup_result );

		// m_path was deleted as a duplicate; nothing left at this path to path-check or move
		if ( cleanup_result.value() ) co_return {};
	}

	// now check if the file is in the right path
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
	const auto current_mime { co_await db->execSqlCoro(
		"SELECT mime_id, name FROM file_info JOIN mime USING (mime_id) WHERE record_id = $1 AND mime_id IS NOT NULL",
		m_record_id ) };

	if ( !current_mime.empty() && !current_mime[ 0 ][ "mime_id" ].isNull() )
	{
		m_mime_name = current_mime[ 0 ][ 1 ].as< std::string >();
		log::trace( "Found that record {} has mime {}", m_record_id, m_mime_name );
		co_return true;
	}

	log::trace( "Record {} does not have a mime", m_record_id );
	co_return false;
}

ExpectedTask< void > ScanContext::scanMime( DbClientPtr db )
{
	FGL_ASSERT( m_record_id != INVALID_RECORD, "Invalid record" );
	auto file_io { std::make_shared< FileIOUring >( m_path ) };

	// skip checking if we have a mime if we are going to rescan it
	if ( !m_params.rescan_mime && co_await hasMime( db ) )
	{
		log::trace( "Skipping metadata scan because it already had metadata and rescan_mime was set to false" );
		co_return {};
	}

	log::trace( "Starting mime scan for {} (Record {})", m_path.filename().string(), m_record_id );
	const auto mime_string_e { co_await mime::getMimeDatabase()->scan( file_io ) };
	log::trace(
		"Mime scan completed for {} (Record {}), result: {}",
		m_path.filename().string(),
		m_record_id,
		mime_string_e.has_value() ? mime_string_e.value() : "nullopt" );

	const auto mtime { filesystem::getLastWriteTime( m_path ) };
	log::trace( "File mtime retrieved for {} (Record {}): {}", m_path.filename().string(), m_record_id, mtime );

	if ( !mime_string_e )
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
			"INSERT INTO file_info (record_id, size, extension, modified_time, cluster_id) VALUES ($1, $2, $3, $4, $5) ON CONFLICT (record_id) DO UPDATE SET extension = $3, mime_id = NULL",
			m_record_id,
			m_size,
			extension_str,
			mtime,
			m_cluster_id );

		co_return {};
	}

	log::trace( "Detected mime {} for file {} (Record {})", *mime_string_e, m_path.filename().string(), m_record_id );
	m_mime_name = *mime_string_e;

	const auto mime_id_e { co_await getMimeIDFromStr( *mime_string_e, db ) };

	return_unexpected_error( mime_id_e );

	log::trace( "Resolved mime '{}' to mime_id={} for record {}", *mime_string_e, *mime_id_e, m_record_id );

	log::trace( "Upserting file_info for record {} with mime_id={}", m_record_id, *mime_id_e );
	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, size, mime_id, modified_time, cluster_id) VALUES ($1, $2, $3, $4, $5) ON CONFLICT (record_id) DO UPDATE SET mime_id = $3",
		m_record_id,
		m_size,
		*mime_id_e,
		mtime,
		m_cluster_id );

	{
		const auto mime_info { co_await db->execSqlCoro( "SELECT 1 FROM mime WHERE mime_id = $1", *mime_id_e ) };
		if ( mime_info.empty() )
			co_return std::unexpected(
				createInternalError( "When selecting mime id {} the DB returned zero rows", *mime_id_e ) );
	}

	co_return {};
}

ExpectedTask< void > ScanContext::scanMetadata( DbClientPtr db )
{
	// No mime was found in the previous step
	if ( m_mime_name.empty() )
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
			// we are not wanting to rescan metadata, so we abort silently.
			log::trace(
				"Skipping metadata scan for {} because it's already been parsed and rescan_metadata was false",
				m_record_id );
			co_return {};
		}
	}

	const std::shared_ptr< modules::RemoteModule > metadata_parser { co_await metadata::findBestParser( m_mime_name ) };

	// No parser was found
	if ( !metadata_parser )
	{
		log::trace( "No metadata parser found for mime {} (Record {})", m_mime_name, m_record_id );
		co_return std::unexpected( createInternalError(
			"Unable to determine metadata parser for {}: No metadata parser for {}", m_record_id, m_mime_name ) );
	}

	log::trace( "Found metadata parser for mime {} (Record {})", m_mime_name, m_record_id );

	auto input_e { modules::CallInput::forPath( m_path ) };

	if ( !input_e )
	{
		co_return std::unexpected(
			createInternalError( "Failed to open file for record {}: {}", m_record_id, input_e.error() ) );
	}

	const modules::RemoteCallData call_data {
		.input = std::make_shared< const modules::CallInput >( std::move( *input_e ) ),
		.mime_name = m_mime_name,
		.extra = {},
		.depth = 0
	};
	const auto metadata_e { co_await metadata_parser->parseFile( call_data ) };

	if ( metadata_e )
	{
		log::trace( "Updating metadata for record {}", m_record_id );
		co_await metadata::updateRecordMetadata( m_record_id, db, *metadata_e );
		log::trace( "Metadata updated for record {}", m_record_id );
	}
	else
	{
		const ModuleError module_error { metadata_e.error() };
		log::warn( "Metadata parser failed for record {}: {}", m_record_id, module_error );
		co_return std::unexpected(
			createInternalError( "Could not parse record {} using metadata parser: {}", m_record_id, module_error ) );
	}

	co_return {};
}

ExpectedTask< void > ScanContext::checkExtension( DbClientPtr db )
{
	log::trace( "Starting extension check for Record {} ({})", m_record_id, m_path.filename().string() );

	auto getExtensionFromMimeName = [ & ]() -> drogon::Task< std::string >
	{
		log::trace( "Looking up extension from mime name '{}'", m_mime_name );
		const auto result {
			co_await db->execSqlCoro( "SELECT best_extension FROM mime WHERE name = $1", m_mime_name )
		};

		if ( result.empty() ) co_return std::string {};
		co_return result[ 0 ][ 0 ].as< std::string >();
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
		m_mime_name.empty() ? co_await getExtensionFromRecord() : co_await getExtensionFromMimeName()
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
