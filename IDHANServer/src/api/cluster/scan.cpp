//
// Created by kj16609 on 3/20/25.
//

#include "MetadataModule.hpp"
#include "api/ClusterAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/size.hpp"
#include "filesystem/IOUring.hpp"
#include "filesystem/filesystem.hpp"
#include "fixme.hpp"
#include "hyapi/helpers.hpp"
#include "logging/log.hpp"
#include "metadata/parseMetadata.hpp"
#include "mime/FileInfo.hpp"
#include "mime/MimeDatabase.hpp"
#include "records/records.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::api
{
ExpectedTask< RecordID > adoptOrphan( FileIOUring io_uring, DbClientPtr db )
{
	const auto data { co_await io_uring.readAll() };
	const auto sha256 { SHA256::hash( data.data(), data.size() ) };
	const auto record_result { co_await idhan::helpers::createRecord( sha256, db ) };

	co_return record_result;
}

struct ScanParams
{
	bool read_only:1 { true };
	bool recompute_hash:1 { false };
	bool scan_mime:1 { true };
	bool rescan_mime:1 { false };
	bool scan_metadata:1 { true };
	bool rescan_metadata:1 { false };
	bool stop_on_fail:1 { false };
	bool adopt_orphans:1 { false };
	bool remove_missing_files:1 { false };
	bool trust_filename:1 { false };
	bool fix_extensions:1 { false };
	bool force_readonly:1 { false };
};

ExpectedTask<> scanFile(
	std::filesystem::path file_path,
	std::filesystem::path bad_dir,
	const DbClientPtr& db,
	ClusterID cluster_id,
	ScanParams params );

// Extracts boolean parameters safely
static ScanParams extractScanParams( const drogon::HttpRequestPtr& request )
{
	ScanParams p {};
	p.read_only = true; // set to false when getting the cluster info only if it's not read only
	p.force_readonly = request->getOptionalParameter< bool >( "force_read_only" ).value_or( false );
	p.recompute_hash = request->getOptionalParameter< bool >( "recompute_hash" ).value_or( true );
	p.trust_filename = request->getOptionalParameter< bool >( "trust_filename" ).value_or( false );
	p.scan_mime = request->getOptionalParameter< bool >( "scan_mime" ).value_or( true );
	p.rescan_mime = request->getOptionalParameter< bool >( "rescan_mime" ).value_or( false );
	p.scan_metadata = request->getOptionalParameter< bool >( "scan_metadata" ).value_or( true );
	p.rescan_metadata = request->getOptionalParameter< bool >( "rescan_metadata" ).value_or( true );
	p.stop_on_fail = request->getOptionalParameter< bool >( "stop_on_fail" ).value_or( true );
	p.adopt_orphans = request->getOptionalParameter< bool >( "adopt_orphans" ).value_or( false );
	p.remove_missing_files = request->getOptionalParameter< bool >( "remove_missing_files" ).value_or( false );

	p.scan_metadata |= p.adopt_orphans; // orphans will need to be scanned for metadata
	p.scan_mime |= p.scan_metadata; // mime is needed for metadata
	p.recompute_hash |= p.adopt_orphans;
	p.recompute_hash |= p.read_only;
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

	static constexpr auto INVALID_RECORD { std::numeric_limits< RecordID >::max() };
	RecordID m_record_id { INVALID_RECORD };

	ClusterID m_cluster_id;
	std::filesystem::path m_cluster_path;

	ExpectedTask< SHA256 > checkSHA256( std::filesystem::path bad_dir );

	ExpectedTask< RecordID > checkRecord( DbClientPtr db );

	ExpectedTask< void > cleanupDoubleClusters( ClusterID found_cluster_id, DbClientPtr db );
	drogon::Task<> updateFileModifiedTime( drogon::orm::DbClientPtr db );
	ExpectedTask< void > insertFileInfo( drogon::orm::DbClientPtr db );

	ExpectedTask< void > checkCluster( DbClientPtr db );
	drogon::Task< bool > hasMime( DbClientPtr db );

	ExpectedTask<> scanMime( DbClientPtr db );

	ExpectedTask<> scanMetadata( DbClientPtr db );
	ExpectedTask< void > checkExtension( DbClientPtr db );

  public:

	ScanContext(
		const std::filesystem::path& file_path,
		const ClusterID cluster_id,
		const std::filesystem::path& cluster_path,
		const ScanParams params ) :
	  m_path( file_path ),
	  m_size( std::filesystem::file_size( file_path ) ),
	  m_params( params ),
	  m_cluster_id( cluster_id ),
	  m_cluster_path( cluster_path )
	{}

	ExpectedTask<> scan( std::filesystem::path bad_dir, DbClientPtr db );
};

drogon::Task< drogon::HttpResponsePtr > ClusterAPI::scan( drogon::HttpRequestPtr request, const ClusterID cluster_id )
{
	const auto db { drogon::app().getDbClient() };
	auto scan_params { extractScanParams( request ) };

	const auto result {
		co_await db->execSqlCoro( "SELECT folder_path, read_only FROM file_clusters WHERE cluster_id = $1", cluster_id )
	};

	scan_params.read_only = scan_params.force_readonly || result[ 0 ][ "read_only" ].as< bool >();

	const std::filesystem::path cluster_path { result[ 0 ][ "folder_path" ].as< std::string >() };

	const auto bad_dir { cluster_path / "bad" };

	std::vector< drogon::Task< std::expected< void, drogon::HttpResponsePtr > > > scan_tasks {};

	std::filesystem::path last_scanned { "" };

	std::vector< ExpectedTask<> > awaiters {};

	for ( const auto& folder : std::filesystem::directory_iterator( cluster_path ) )
	{
		if ( !folder.is_directory() ) continue;

		if ( folder.path() == bad_dir ) continue;

		for ( const auto& file : std::filesystem::directory_iterator( folder ) )
		{
			const auto entry { file };

			const auto& file_path { entry.path() };

			log::info( "Scanner hitting path: {}", file_path.string() );

			if ( !entry.is_regular_file() )
			{
				continue;
			}

			// ignore thumbnails
			if ( file_path.extension() == ".thumbnail" )
			{
				continue;
			}

			if ( file_path.parent_path() != last_scanned )
			{
				last_scanned = file_path.parent_path();
				log::info( "Scanning {}", last_scanned.string() );
			}

			ScanContext ctx { file_path, cluster_id, cluster_path, scan_params };

			const std::expected< void, drogon::HttpResponsePtr > file_result { co_await ctx.scan( bad_dir, db ) };

			if ( scan_params.stop_on_fail && !file_result )
			{
				co_return file_result.error();
			};
		}
	}

	co_await drogon::when_all( std::move( scan_tasks ) );

	request->setPath( format_ns::format( "/clusters/{}/info", cluster_id ) );
	co_return co_await drogon::app().forwardCoro( request );
}

/**
 *
 * @param uring
 * @param bad_dir Directory to put failed files, Such as ones that have the wrong filename
 * @return
 */
ExpectedTask< SHA256 > ScanContext::checkSHA256( const std::filesystem::path bad_dir )
{
	const auto file_stem { m_path.stem().string() };
	FileIOUring uring { m_path };

	auto sha256_e { m_params.trust_filename ? SHA256::fromHex( file_stem ) : co_await SHA256::hashCoro( uring ) };

	if ( m_params.trust_filename && !sha256_e )
	{
		// hash the file anyways
		sha256_e = co_await SHA256::hashCoro( uring );
	}

	if ( !sha256_e ) co_return std::unexpected( sha256_e.error() );

	const auto sha256_hex { sha256_e->hex() };
	if ( sha256_hex != file_stem )
	{
		log::warn(
			"While scanning file at {} it was detected that the filename does not match the SHA256 {}",
			m_path.string(),
			sha256_hex );

		if ( m_params.read_only )
		{
			co_return std::unexpected( createInternalError(
				"When scanning file at {} it was detected that the filename does not match the sha256 "
				"{}. Because the cluster is in readonly mode, This could not be automatically fixed",
				m_path.string(),
				sha256_hex ) );
		}

		try
		{
			if ( !m_params.read_only )
			{
				std::filesystem::create_directories( bad_dir );
				const auto new_path { bad_dir / m_path.filename() };

				// try to fix the mistake
				std::filesystem::rename( m_path, new_path );

				co_return std::unexpected( createInternalError(
					"When scanning file at {} it was detected that the filename does "
					"not match the sha256 {}. The file has been moved to {}",
					m_path.string(),
					sha256_hex,
					new_path.string() ) );
			}
		}
		catch ( std::exception& e )
		{
			co_return std::unexpected( createInternalError(
				"When scanning file at {} it was detected that the filename does not match the sha256 "
				"{}. There was an error that prevented this from being fixed: {}",
				m_path.string(),
				sha256_hex,
				e.what() ) );
		}
		catch ( ... )
		{
			co_return std::unexpected( createInternalError(
				"When scanning file at {} it was detected that the filename does not match the sha256 "
				"{}. There was an unknown error that prevented this from being fixed",
				m_path.string(),
				sha256_hex ) );
		}
	}

	co_return *sha256_e;
}

ExpectedTask< RecordID > ScanContext::checkRecord( drogon::orm::DbClientPtr db )
{
	const auto search_result {
		co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", m_sha256.toVec() )
	};

	if ( search_result.empty() && m_params.adopt_orphans )
	{
		const auto insert_result {
			co_await db->execSqlCoro( "INSERT INTO records (sha256) VALUES ($1) RETURNING record_id", m_sha256.toVec() )
		};

		if ( insert_result.empty() )
		{
			co_return std::unexpected( createInternalError( "Failed to create a record for hash {}", m_sha256.hex() ) );
		}

		co_return insert_result[ 0 ][ 0 ].as< RecordID >();
	}
	else if ( search_result.empty() )
	{
		co_return std::unexpected( createInternalError(
			"When scanning cluster {} file {} was not found as a existing record and scan was not set to adopt orphans",
			m_cluster_id,
			m_path.string() ) );
	}

	co_return search_result[ 0 ][ 0 ].as< RecordID >();
}

ExpectedTask< void > ScanContext::cleanupDoubleClusters( const ClusterID found_cluster_id, drogon::orm::DbClientPtr db )
{
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
		}

		co_return {};
	}

	log::warn(
		"File {} was missing from it's expected cluster of {}. Setting the record as being stored in cluster {} instead",
		m_record_id,
		found_cluster_id,
		m_cluster_id );

	co_return {};
}

drogon::Task<> ScanContext::updateFileModifiedTime( drogon::orm::DbClientPtr db )
{
	const trantor::Date date { filesystem::getLastWriteTime( m_path ) };

	log::debug( "mtime is {}", date.toFormattedString( true ) );

	co_await db->execSqlCoro( "UPDATE file_info SET modified_time = $1 WHERE record_id = $2", date, m_record_id );
}

ExpectedTask<> ScanContext::insertFileInfo( drogon::orm::DbClientPtr db )
{
	const trantor::Date date { filesystem::getLastWriteTime( m_path ) };
	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, cluster_id, size, modified_time) VALUES ($1, $2, $3, $4)",
		m_record_id,
		m_cluster_id,
		m_size,
		date );
	co_return {};
}

ExpectedTask<> ScanContext::checkCluster( drogon::orm::DbClientPtr db )
{
	log::debug( "Verifying that the record is in the correct cluster" );
	FGL_ASSERT( m_record_id != INVALID_RECORD, "Invalid record" );
	const auto file_info {
		co_await db->execSqlCoro( "SELECT cluster_id, modified_time FROM file_info WHERE record_id = $1", m_record_id )
	};

	// create the file info if it doesn't already exist
	if ( file_info.empty() )
	{
		co_return co_await insertFileInfo( db );
	}

	if ( file_info[ 0 ][ "modified_time" ].isNull() )
	{
		co_await updateFileModifiedTime( db );
	}

	// we found a cluster, check if it's the one we are about to add too
	const auto found_cluster_id { file_info[ 0 ][ 0 ].as< ClusterID >() };
	const bool clusters_match { found_cluster_id == m_cluster_id };

	if ( !clusters_match )
	{
		// handle the double count, which will check if the found cluster contains the file and delete it from this one
		// if found. Otherwise the record's cluster is set to the current cluster
		auto result { co_await cleanupDoubleClusters( found_cluster_id, db ) };
		return_unexpected_error( result );
	}

	// now check if the file is in the right path
	const auto current_parent { m_path.parent_path() };
	const auto expected_cluster_subfolder { filesystem::getFileFolder( m_sha256 ) };
	const auto expected_parent_path { m_cluster_path / expected_cluster_subfolder };

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
			log::info( "Moving file {} to {}", new_path.string(), new_path.string() );

			std::filesystem::create_directories( expected_parent_path );

			std::filesystem::rename( m_path, new_path );

			m_path = new_path;
		}
	}

	co_return {};
}

drogon::Task< bool > ScanContext::hasMime( DbClientPtr db )
{
	auto current_mime { co_await db->execSqlCoro(
		"SELECT mime_id, name FROM file_info JOIN mime USING (mime_id) WHERE record_id = $1 AND mime_id IS NOT NULL",
		m_record_id ) };

	if ( !current_mime.empty() && !current_mime[ 0 ][ "mime_id" ].isNull() )
	{
		m_mime_name = current_mime[ 0 ][ 1 ].as< std::string >();
		co_return true;
	}

	co_return false;
}

ExpectedTask<> ScanContext::scanMime( DbClientPtr db )
{
	FGL_ASSERT( m_record_id != INVALID_RECORD, "Invalid record" );
	FileIOUring file_io { m_path };

	// skip checking if we have a mime if we are going to rescan it
	if ( ( !m_params.rescan_mime ) && co_await hasMime( db ) )
	{
		log::debug( "Skipping metadata scan because it already had metadata and rescan_mime was set to false" );
		co_return {};
	}

	log::debug( "Starting metadata scan for {} (Record {})", m_path.filename().string(), m_record_id );
	const auto mime_string_e { co_await mime::getMimeDatabase()->scan( file_io ) };

	const auto mtime { filesystem::getLastWriteTime( m_path ) };

	if ( !mime_string_e )
	{
		std::string extension_str { m_path.extension().string() };

		if ( extension_str.starts_with( "." ) ) extension_str = extension_str.substr( 1 );

		log::warn(
			"During a cluster scan file {} failed to be detected by any mime parsers; It has been added despite this and has an extension override of \'{}\' (Record {})",
			m_path.filename().string(),
			extension_str,
			m_record_id );

		co_await db->execSqlCoro(
			"INSERT INTO file_info (record_id, size, extension, modified_time) VALUES ($1, $2, $3, $4) ON CONFLICT (record_id) DO UPDATE SET extension = $3, mime_id = NULL",
			m_record_id,
			m_size,
			extension_str,
			mtime );

		co_return {};
	}

	m_mime_name = *mime_string_e;

	const auto mime_id_e { co_await getMimeIDFromStr( *mime_string_e, db ) };

	return_unexpected_error( mime_id_e );

	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, size, mime_id, modified_time) VALUES ($1, $2, $3, $4) ON CONFLICT (record_id) DO UPDATE SET mime_id = $3",
		m_record_id,
		m_size,
		*mime_id_e,
		mtime );

	const auto mime_info {
		co_await db->execSqlCoro( "SELECT best_extension FROM mime WHERE mime_id = $1", *mime_id_e )
	};
	if ( mime_info.empty() )
		co_return std::unexpected(
			createInternalError( "When selecting mime id {} the DB returned zero rows", *mime_id_e ) );

	const auto expected_extension { mime_info[ 0 ][ 0 ].as< std::string >() };

	co_return {};
}

ExpectedTask<> ScanContext::scanMetadata( DbClientPtr db )
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
			log::debug(
				"Skipping metadata scan for {} because it's already been parsed and rescan_metadata was false",
				m_record_id );
			co_return {};
		}
	}

	const std::shared_ptr< MetadataModuleI > metadata_parser { co_await metadata::findBestParser( m_mime_name ) };

	// No parser was found
	if ( !metadata_parser )
	{
		co_return std::unexpected( createInternalError(
			"Unable to determine metadata parser for {}: No metadata parser for {}", m_record_id, m_mime_name ) );
	}

	//TODO: In order to protect the main process from poorly written modules, Either fork or launch a new process to actually run the parser

	FileIOUring file_io { m_path };

	std::vector< std::byte > file_data { co_await file_io.readAll() };

	const auto metadata_e { metadata_parser->parseFile( file_data.data(), file_data.size(), m_mime_name ) };

	if ( metadata_e )
	{
		co_await metadata::updateRecordMetadata( m_record_id, db, *metadata_e );
	}
	else
	{
		const ModuleError module_error { metadata_e.error() };
		co_return std::unexpected(
			createInternalError( "Could not parse record {} using metadata parser: {}", m_record_id, module_error ) );
	}

	co_return {};
}

ExpectedTask< void > ScanContext::checkExtension( DbClientPtr db )
{
	auto getExtensionFromMimeName = [ & ]() -> drogon::Task< std::string >
	{
		const auto result {
			co_await db->execSqlCoro( "SELECT best_extension FROM mime WHERE name = $1", m_mime_name )
		};

		co_return result[ 0 ][ 0 ].as< std::string >();
	};

	auto getExtensionFromRecord = [ & ]() -> drogon::Task< std::string >
	{
		const auto result { co_await db->execSqlCoro(
			"SELECT COALESCE(best_extension, extension) FROM file_info LEFT JOIN mime USING (mime_id) WHERE record_id = $1",
			m_record_id ) };

		co_return result[ 0 ][ 0 ].as< std::string >();
	};

	const auto expected_extension {
		m_mime_name.empty() ? co_await getExtensionFromRecord() : co_await getExtensionFromMimeName()
	};

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
			auto new_path = m_path.replace_extension( format_ns::format( ".{}", expected_extension ) );
			std::filesystem::rename( m_path, new_path );
			log::info( "Renamed file {} to {} due to extension mismatch", m_path.string(), new_path.string() );
			m_path = new_path;
		}
	}
	else
	{
		log::debug( "{} passed it's extension check. Expected {}", m_mime_name, file_extension );
	}

	co_return {};
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > ScanContext::scan(
	const std::filesystem::path bad_dir,
	drogon::orm::DbClientPtr db )
{
	log::debug( "Scanning file: {}", m_path.string() );

	if ( m_size == 0 )
		co_return std::unexpected( createInternalError(
			"When scanning file: {} it was detected that it has a filesize of zero!", m_path.string() ) );

	// check that the sha256 matches the sha256 name of the file
	const auto sha256_e { co_await checkSHA256( bad_dir ) };
	return_unexpected_error( sha256_e );

	m_sha256 = *sha256_e;

	const auto record_e { co_await checkRecord( db ) };
	if ( !record_e ) co_return std::unexpected( record_e.error() );
	return_unexpected_error( record_e );
	m_record_id = *record_e;

	log::debug( "File {} was detected as record {}", m_path.string(), m_record_id );

	// check if the record has been identified in a cluster before
	const auto cluster_e { co_await checkCluster( db ) };

	bool has_mime_info { co_await hasMime( db ) };

	if ( ( m_params.scan_mime && !has_mime_info ) || m_params.rescan_mime )
	{
		log::debug( "Scanning mime for file {}", m_path.string() );
		const auto mime_e { co_await scanMime( db ) };
		if ( !mime_e )
		{
			const auto msg( hyapi::helpers::extractHttpResponseErrorMessage( mime_e.error() ) );
			log::warn( "Failed to process mime for {} (Record {}): {}", m_path.filename().string(), m_record_id, msg );
			co_return std::unexpected( createInternalError(
				"Failed to process mime for {} (Record {}): {}", m_path.filename().string(), m_record_id, msg ) );
		}
		has_mime_info = co_await hasMime( db );
	}

	if ( has_mime_info )
	{
		// extension check
		const auto extenion_result { co_await checkExtension( db ) };
		return_unexpected_error( extenion_result );
	}

	if ( ( m_params.scan_metadata || m_params.rescan_metadata ) && has_mime_info )
	{
		log::debug( "Scanning metadata for file {}", m_path.string() );
		const auto metadata_e { co_await scanMetadata( db ) };
		if ( !metadata_e ) co_return std::unexpected( metadata_e.error() );
	}

	log::debug( "Finished scanning file {}", m_path.string() );

	co_return {};
}
} // namespace idhan::api
