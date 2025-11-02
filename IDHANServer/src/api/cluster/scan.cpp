//
// Created by kj16609 on 3/20/25.
//

#include "MetadataModule.hpp"
#include "api/ClusterAPI.hpp"
#include "api/helpers/ExpectedTask.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/records.hpp"
#include "crypto/SHA256.hpp"
#include "fgl/size.hpp"
#include "filesystem/IOUring.hpp"
#include "filesystem/utility.hpp"
#include "fixme.hpp"
#include "hyapi/helpers.hpp"
#include "logging/log.hpp"
#include "metadata/parseMetadata.hpp"
#include "mime/FileInfo.hpp"
#include "mime/MimeDatabase.hpp"

namespace idhan::api
{
ExpectedTask< RecordID > adoptOrphan( FileIOUring io_uring, DbClientPtr db )
{
	const auto data { co_await io_uring.readAll() };
	const auto sha256 { SHA256::hash( data.data(), data.size() ) };
	const auto record_result { co_await helpers::createRecord( sha256, db ) };

	co_return record_result;
}

struct ScanParams
{
	bool read_only:1;
	bool recompute_hash:1;
	bool scan_mime:1;
	bool rescan_mime:1;
	bool scan_metadata:1;
	bool rescan_metadata:1;
	bool stop_on_fail:1;
	bool adopt_orphans:1;
	bool remove_missing_files:1;
	bool trust_filename:1;
	bool fix_extensions:1;
	bool force_readonly:1;
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

	p.scan_metadata = p.scan_metadata || p.adopt_orphans; // orphans will need to be scanned for metadata
	p.scan_mime = p.scan_mime || p.scan_metadata; // mime is needed for metadata
	p.recompute_hash = p.recompute_hash || p.adopt_orphans;
	p.recompute_hash = p.recompute_hash || p.read_only;
	// if read only then we need to recompute the hash because the file path can't be trusted anymore

	return p;
}

class ScanContext
{
	std::filesystem::path m_path;
	std::size_t m_size;

	ScanParams m_params;
	std::string m_mime_name {};

	static constexpr auto INVALID_RECORD { std::numeric_limits< RecordID >::max() };
	RecordID m_record_id { INVALID_RECORD };

	ClusterID m_cluster_id;

	ExpectedTask< SHA256 > checkSHA256( FileIOUring uring, std::filesystem::path bad_dir );

	ExpectedTask< RecordID > checkRecord( SHA256 sha256, DbClientPtr db );

	ExpectedTask< void > cleanupDoubleClusters( ClusterID found_cluster_id, DbClientPtr db );

	ExpectedTask< void > checkCluster( DbClientPtr db );

	ExpectedTask<> scanMime( DbClientPtr db );

	ExpectedTask<> scanMetadata( DbClientPtr db );
	ExpectedTask< void > checkExtension( DbClientPtr db );

  public:

	ScanContext( const std::filesystem::path& file_path, const ClusterID cluster_id, const ScanParams params ) :
	  m_path( file_path ),
	  m_size( std::filesystem::file_size( file_path ) ),
	  m_params( params ),
	  m_cluster_id( cluster_id )
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

	const std::filesystem::path folder_path { result[ 0 ][ "folder_path" ].as< std::string >() };

	const auto bad_dir { folder_path / "bad" };

	std::vector< drogon::Task< std::expected< void, drogon::HttpResponsePtr > > > scan_tasks {};

	std::filesystem::path last_scanned { "" };

	auto dir_itterator { std::filesystem::recursive_directory_iterator( folder_path ) };
	const auto end { std::filesystem::recursive_directory_iterator() };

	std::vector< ExpectedTask<> > awaiters {};

	while ( dir_itterator != end )
	{
		const auto entry { *dir_itterator };

		const auto& file_path { entry.path() };

		if ( file_path == bad_dir )
		{
			dir_itterator.disable_recursion_pending();
		}

		if ( !entry.is_regular_file() )
		{
			++dir_itterator;
			continue;
		}

		// ignore thumbnails
		if ( file_path.extension() == ".thumbnail" )
		{
			++dir_itterator;
			continue;
		}

		if ( file_path.parent_path() != last_scanned )
		{
			last_scanned = file_path.parent_path();
			log::info( "Scanning {}", last_scanned.string() );
		}

		ScanContext ctx { file_path, cluster_id, scan_params };

		const std::expected< void, drogon::HttpResponsePtr > file_result { co_await ctx.scan( bad_dir, db ) };

		if ( scan_params.stop_on_fail && !file_result )
		{
			co_return file_result.error();
		};

		++dir_itterator;
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
ExpectedTask< SHA256 > ScanContext::checkSHA256( FileIOUring uring, const std::filesystem::path bad_dir )
{
	const auto file_stem { m_path.stem().string() };

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

		// try to fix the mistake
		auto new_path { bad_dir / m_path.filename() };

		try
		{
			std::filesystem::rename( m_path, new_path );
		}
		catch ( std::exception& e )
		{
			co_return std::unexpected( createInternalError(
				"When scanning file at {} it was detected that the filename does not match the sha256 "
				"{}. There was an error attempting to fix this: What: {}",
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

		co_return std::unexpected( createInternalError(
			"When scanning file at {} it was detected that the filename does "
			"not match the sha256 {}. The file has been moved to {}",
			m_path.string(),
			sha256_hex,
			new_path.string() ) );
	}

	co_return *sha256_e;
}

ExpectedTask< RecordID > ScanContext::checkRecord( const SHA256 sha256, drogon::orm::DbClientPtr db )
{
	const auto search_result {
		co_await db->execSqlCoro( "SELECT record_id FROM records WHERE sha256 = $1", sha256.toVec() )
	};

	if ( search_result.empty() && m_params.adopt_orphans )
	{
		const auto insert_result {
			co_await db->execSqlCoro( "INSERT INTO records (sha256) VALUES ($1) RETURNING record_id", sha256.toVec() )
		};

		if ( insert_result.empty() )
		{
			co_return std::unexpected( createInternalError( "Failed to create a record for hash {}", sha256.hex() ) );
		}

		co_return insert_result[ 0 ][ 0 ].as< RecordID >();
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
	}

	log::warn(
		"File {} was missing from it's expected cluster of {}. Setting the record as being stored in cluster {} instead",
		m_record_id,
		found_cluster_id,
		m_cluster_id );

	co_return {};
}

ExpectedTask<> ScanContext::checkCluster( drogon::orm::DbClientPtr db )
{
	FGL_ASSERT( m_record_id != INVALID_RECORD, "Invalid record" );
	const auto file_info {
		co_await db->execSqlCoro( "SELECT cluster_id FROM file_info WHERE record_id = $1", m_record_id )
	};

	if ( file_info.empty() )
	{
		co_await db->execSqlCoro(
			"INSERT INTO file_info (record_id, cluster_id, size, cluster_store_time) VALUES ($1, $2, $3, now())",
			m_record_id,
			m_cluster_id,
			m_size );

		co_return {};
	}

	// we found a cluster, check if it's the one we are about to add too
	const auto found_cluster_id { file_info[ 0 ][ 0 ].as< ClusterID >() };
	const bool clusters_match { found_cluster_id == m_cluster_id };

	if ( !clusters_match )
	{
		// handle the double count, which will check if the found cluster contains the file and delete it from this one
		// if found. Otherwise the record's cluster is set to the current cluster
		auto result { co_await cleanupDoubleClusters( found_cluster_id, db ) };
		if ( !result ) co_return std::unexpected( result.error() );
	}

	co_return {};
}

ExpectedTask<> ScanContext::scanMime( DbClientPtr db )
{
	FGL_ASSERT( m_record_id != INVALID_RECORD, "Invalid record" );
	FileIOUring file_io { m_path };

	if ( !m_params.rescan_mime ) // skip checking if we have a mime if we are going to rescan it
	{
		auto current_mime { co_await db->execSqlCoro(
			"SELECT mime_id, name FROM file_info JOIN mime USING (mime_id) WHERE record_id = $1 AND mime_id IS NOT NULL",
			m_record_id ) };

		if ( !current_mime.empty() )
		{
			m_mime_name = current_mime[ 0 ][ 1 ].as< std::string >();
			co_return {};
		}
	}

	const auto mime_string_e { co_await mime::getMimeDatabase()->scan( file_io ) };

	if ( !mime_string_e )
	{
		std::string extension_str { m_path.extension().string() };

		if ( extension_str.starts_with( "." ) ) extension_str = extension_str.substr( 1 );

		log::warn(
			"During a cluster scan file {} failed to be detected by any mime parsers; It has been added despite this and has an extension override of \'{}\'",
			m_path.string(),
			extension_str );

		co_await db->execSqlCoro(
			"INSERT INTO file_info (record_id, size, extension, cluster_store_time) VALUES ($1, $2, $3, now()) ON CONFLICT (record_id) DO UPDATE SET extension = $3, mime_id = NULL",
			m_record_id,
			m_size,
			extension_str );

		co_return {};
	}

	m_mime_name = *mime_string_e;

	const auto mime_id_e { co_await getMimeIDFromStr( *mime_string_e, db ) };

	return_unexpected_error( mime_id_e );

	co_await db->execSqlCoro(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_store_time) VALUES ($1, $2, $3, now()) ON CONFLICT (record_id) DO UPDATE SET mime_id = $3",
		m_record_id,
		m_size,
		*mime_id_e );

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
		co_return std::unexpected(
			createInternalError( "Unable to determine metadata parser for {}: No mime found", m_record_id ) );
	}

	if ( !m_params.rescan_metadata )
	{
		const auto current_metadata {
			co_await db->execSqlCoro( "SELECT 1 FROM metadata WHERE record_id = $1", m_record_id )
		};
		if ( !current_metadata.empty() )
		{
			// we are not wanting to rescan metadata, so we abort silently.
			co_return {};
		}
	}

	const std::shared_ptr< MetadataModuleI > metadata_parser { co_await findBestParser( m_mime_name ) };

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
		co_await updateRecordMetadata( m_record_id, db, *metadata_e );
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
	const auto info_result {
		co_await db->execSqlCoro( "SELECT best_extension FROM mime WHERE mime = $1", m_mime_name )
	};

	const auto expected_extension { info_result[ 0 ][ 0 ].as< std::string >() };

	std::string file_extension { m_path.extension().string() };
	if ( file_extension.starts_with( "." ) ) file_extension = file_extension.substr( 1 );

	if ( expected_extension != file_extension )
	{
		log::warn(
			"When scanning record {}. It was detected that the extension did not match it's mime, Expected {} got {}",
			m_record_id,
			expected_extension,
			file_extension );

		if ( !m_params.read_only && m_params.fix_extensions )
		{
			auto new_path = m_path.replace_extension( format_ns::format( ".{}", expected_extension ) );
			std::filesystem::rename( m_path, new_path );
			log::info( "Renamed file {} to {} due to extension mismatch", m_path.string(), new_path.string() );
			m_path = new_path;
		}
	}

	co_return {};
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > ScanContext::scan(
	const std::filesystem::path bad_dir,
	drogon::orm::DbClientPtr db )
{
	FileIOUring io_uring { m_path };

	log::debug( "Scanning file: {}", m_path.string() );

	if ( m_size == 0 )
		co_return std::unexpected( createInternalError(
			"When scanning file: {} it was detected that it has a filesize of zero!", m_path.string() ) );

	const auto sha256_e { co_await checkSHA256( io_uring, bad_dir ) };
	return_unexpected_error( sha256_e );

	const auto record_e { co_await checkRecord( *sha256_e, db ) };
	if ( !record_e ) co_return std::unexpected( record_e.error() );
	return_unexpected_error( record_e );
	m_record_id = *record_e;

	log::debug( "File {} was detected as record {}", m_path.string(), m_record_id );

	// check if the record has been identified in a cluster before
	const auto cluster_e { co_await checkCluster( db ) };

	if ( m_params.scan_mime )
	{
		log::debug( "Scanning mime for file {}", m_path.string() );
		const auto mime_e { co_await scanMime( db ) };
		if ( !mime_e )
		{
			const auto msg( hyapi::helpers::extractHttpResponseErrorMessage( mime_e.error() ) );
			log::warn( "Failed to process mime for record {} at path {}: {}", m_record_id, m_path.string(), msg );
			co_return std::unexpected( createInternalError(
				"Failed to process mime for record {} at path {}: {}", m_record_id, m_path.string(), msg ) );
		}
	}

	// extension check
	const auto extenion_result { co_await checkExtension( db ) };
	return_unexpected_error( extenion_result );

	if ( m_params.scan_metadata )
	{
		log::debug( "Scanning metadata for file {}", m_path.string() );
		const auto metadata_e { co_await scanMetadata( db ) };
		if ( !metadata_e ) co_return std::unexpected( metadata_e.error() );
	}

	log::debug( "Finished scanning file {}", m_path.string() );

	co_return {};
}
} // namespace idhan::api
