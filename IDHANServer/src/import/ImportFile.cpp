#include "ImportFile.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "MimeIDs.hpp"
#include "filesystem/PathHelpers.hpp"
#include "filesystem/VerifiedRecordPath.hpp"
#include "filesystem/clusters/clusterIdentity.hpp"
#include "filesystem/hashFile.hpp"
#include "filesystem/recordFilePath.hpp"
#include "httpapi/fileMetadata.hpp"
#include "httpapi/records.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "logging/log.hpp"
#include "mime/identifyMime.hpp"

namespace idhan::imports
{

[[nodiscard]] static runtime::Task< std::expected< bool, ImportError > > storedFileIsValid(
	db::DBContext& db,
	const RecordID record_id,
	const SHA256& expected_sha256 )
{
	auto path_e { co_await httpapi::recordFilePathForScan( db, record_id ) };
	if ( !path_e )
	{
		if ( path_e.error().message.getStatusCode() == 503 )
			co_return std::unexpected( ImportError { 503, "Assigned cluster identity could not be verified" } );
		co_return false;
	}

	try
	{
		const auto actual { co_await filesystem::hashFile( path_e->path() ) };
		const auto valid { path_e->verify() };
		if ( !valid ) co_return std::unexpected( ImportError { 503, valid.error() } );

		co_return actual == expected_sha256;
	}
	catch ( const std::exception& )
	{
		co_return false;
	}
}

struct ImportTarget
{
	ClusterID id;
	filesystem::ClusterAccess access;
};

[[nodiscard]] static runtime::Task< std::expected< ImportTarget, ImportError > > findBestFolderForImport(
	db::DBContext& db,
	const RecordID record_id )
{
	static constexpr char query[] {
#embed "sql/current_import_cluster.sql" suffix(, )
		'\0'
	};

	db::DBBindings current_bindings {};
	current_bindings.bind( record_id );
	const auto current { co_await db.query( query, std::move( current_bindings ) )
		                     .rows< std::optional< ClusterID >, bool, std::optional< bool > >() };

	if ( !current.empty() )
	{
		const auto& [ cluster_id, delete_time_null, read_only ] { current.row( 0 ) };
		if ( cluster_id && delete_time_null && read_only && !*read_only )
		{
			auto cluster { co_await filesystem::openCluster( db, *cluster_id ) };
			if ( !cluster ) co_return std::unexpected( ImportError { 503, cluster.error() } );
			co_return ImportTarget { *cluster_id, std::move( *cluster ) };
		}
	}

	const auto clusters {
		co_await db.query( "SELECT cluster_id, size_used, size_limit FROM file_clusters WHERE read_only = FALSE" )
			.rows< ClusterID, std::int64_t, std::int64_t >()
	};

	if ( clusters.empty() )
	{
		const auto total { co_await db.query( "SELECT count(*) FROM file_clusters" ).rows< std::int64_t >() };

		if ( std::get< 0 >( total.row( 0 ) ) == 0 )
			co_return std::unexpected(
				ImportError { 400, "No clusters available, You must create one before importing files" } );

		co_return std::unexpected(
			ImportError { 400, "All clusters are read-only, You must mark one as writable before importing files" } );
	}

	struct ClusterCandidate
	{
		ClusterID id {};
		double score {};
	};

	std::vector< ClusterCandidate > candidates {};
	candidates.reserve( clusters.size() );

	for ( const auto& [ cluster_id, size_used, size_limit ] : clusters )
	{
		const double score {
			size_limit <= 0 ? 0.0 : static_cast< double >( size_used ) / static_cast< double >( size_limit )
		};
		candidates.push_back( ClusterCandidate { cluster_id, score } );
	}

	std::ranges::sort( candidates, {}, &ClusterCandidate::score );
	for ( const auto& candidate : candidates )
	{
		auto access { co_await filesystem::openCluster( db, candidate.id ) };
		if ( !access ) continue;

		co_return ImportTarget { candidate.id, std::move( *access ) };
	}

	co_return std::unexpected( ImportError { 503, "No writable cluster has a verified identity" } );
}

[[nodiscard]] static runtime::Task< std::expected< std::string, ImportError > > recordBestExtension(
	db::DBContext& db,
	const RecordID record_id )
{
	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
			.query(
				"SELECT best_extension FROM mime JOIN file_info USING (mime_id) WHERE file_info.record_id = $1",
				std::move( bindings ) )
			.rows< std::optional< std::string > >()
	};

	if ( rows.empty() )
		co_return std::unexpected( ImportError { 500, std::format( "No file info for record {}", record_id ) } );

	co_return std::get< 0 >( rows.row( 0 ) ).value_or( std::string {} );
}

[[nodiscard]] static std::expected< void, ImportError > writeFileToCluster(
	const std::filesystem::path& cluster_path,
	const bool read_only,
	const SHA256& sha256,
	const std::span< const std::byte > data,
	const std::string_view extension )
{
	if ( read_only ) return std::unexpected( ImportError { 500, "Refusing to write into a read-only cluster" } );

	const auto path {
		std::filesystem::absolute( cluster_path ) / filesystem::getClusterRelativePath( sha256, extension )
	};

	std::error_code error_code {};
	std::filesystem::create_directories( path.parent_path(), error_code );
	if ( error_code )
		return std::unexpected(
			ImportError { 500, std::format( "Failed to create cluster directory: {}", error_code.message() ) } );

	std::ofstream stream( path, std::ios::binary );
	if ( !stream )
		return std::unexpected(
			ImportError { 500, std::format( "Failed to open file {} for writing!", path.string() ) } );

	stream.write( reinterpret_cast< const char* >( data.data() ), static_cast< std::streamsize >( data.size() ) );
	stream.close();
	if ( !stream )
		return std::unexpected( ImportError { 500, std::format( "Failed to write file {}", path.string() ) } );

	return {};
}

[[nodiscard]] static runtime::Task< std::expected< void, ImportError > > storeRecordFile(
	db::DBContext& db,
	const RecordID record_id,
	const SHA256& sha256,
	const std::span< const std::byte > data,
	const ImportTarget& target )
{
	db::DBBindings current_bindings {};
	current_bindings.bind( record_id );
	const auto current {
		co_await db
			.query(
				"SELECT file_clusters.read_only FROM file_info "
				"JOIN file_clusters ON file_clusters.cluster_id = file_info.cluster_id "
				"WHERE file_info.record_id = $1",
				std::move( current_bindings ) )
			.rows< bool >()
	};

	if ( !current.empty() && std::get< 0 >( current.row( 0 ) ) )
	{
		auto valid { co_await storedFileIsValid( db, record_id, sha256 ) };
		if ( !valid ) co_return std::unexpected( std::move( valid.error() ) );
		if ( *valid ) co_return {};

		log::warn(
			"Record {} is stored in a read-only cluster but the file there is missing or corrupt. Storing it "
			"again elsewhere",
			record_id );
	}

	auto extension_e { co_await recordBestExtension( db, record_id ) };
	if ( !extension_e ) co_return std::unexpected( std::move( extension_e.error() ) );

	const auto valid_before_write { target.access.verify() };
	if ( !valid_before_write ) co_return std::unexpected( ImportError { 503, valid_before_write.error() } );

	auto written { writeFileToCluster( target.access.root(), target.access.readonly(), sha256, data, *extension_e ) };
	if ( !written ) co_return std::unexpected( std::move( written.error() ) );

	db::DBBindings update_bindings {};
	update_bindings.bind( record_id );
	update_bindings.bind( target.id );
	co_await db
		.query(
			"UPDATE file_info SET cluster_store_time = now(), cluster_id = $2, cluster_delete_time = NULL "
			"WHERE record_id = $1",
			std::move( update_bindings ) )
		.result();

	co_return {};
}

runtime::Task< std::expected< ImportOutcome, ImportError > > importFile(
	const std::span< const std::byte > data,
	const SHA256& sha256,
	std::string filename,
	const bool force_import,
	db::DBContext& db )
{
	if ( data.empty() ) co_return std::unexpected( ImportError { 400, "No file data supplied" } );

	const auto existing_record { co_await httpapi::findRecord( sha256, db ) };
	if ( existing_record )
	{
		auto assigned { co_await filesystem::openRecordCluster( db, *existing_record ) };
		if ( !assigned ) co_return std::unexpected( ImportError { 503, assigned.error() } );
	}

	if ( !force_import )
	{
		if ( existing_record )
		{
			db::DBBindings ts_bindings {};
			ts_bindings.bind( *existing_record );
			const auto timestamps {
				co_await db
					.query(
						"SELECT cluster_delete_time IS NULL, "
						"extract(epoch FROM cluster_delete_time)::bigint "
						"FROM file_info WHERE record_id = $1",
						std::move( ts_bindings ) )
					.rows< bool, std::optional< std::int64_t > >()
			};

			if ( !timestamps.empty() )
			{
				const auto& [ delete_time_null, delete_epoch ] { timestamps.row( 0 ) };

				if ( !delete_time_null )
					co_return ImportOutcome {
						.status = Deleted, .record_id = *existing_record, .deleted_time = delete_epoch
					};

				auto path_e { co_await httpapi::recordFilePathForScan( db, *existing_record ) };
				if ( path_e ) co_return ImportOutcome { .status = Exists, .record_id = *existing_record };
				if ( path_e.error().message.getStatusCode() == 503 )
					co_return std::unexpected( ImportError { 503, "Assigned cluster identity could not be verified" } );
			}
		}
	}

	const auto mime_id { co_await mime::identifyMime( data, filename ) };

	if ( mime_id == mime_ids::UNKNOWN && !force_import )
		co_return ImportOutcome { .status = Failed, .failure = UnknownMime };

	auto record_id_e { co_await httpapi::createRecord( sha256, db ) };
	if ( !record_id_e )
		co_return std::unexpected(
			ImportError { 500, std::format( "Failed to create a record for {}", sha256.hex() ) } );
	const auto record_id { *record_id_e };

	auto target_cluster_e { co_await findBestFolderForImport( db, record_id ) };
	if ( !target_cluster_e ) co_return std::unexpected( std::move( target_cluster_e.error() ) );

	db::DBBindings insert_bindings {};
	insert_bindings.bind( record_id );
	insert_bindings.bind( mime_id );
	insert_bindings.bind( static_cast< std::int64_t >( data.size() ) );
	insert_bindings.bind( target_cluster_e->id );
	co_await db
		.query(
			"INSERT INTO file_info (record_id, mime_id, size, cluster_id, file_mtime) "
			"VALUES ($1, $2, $3, $4, now()) ON CONFLICT DO NOTHING",
			std::move( insert_bindings ) )
		.result();

	db::DBBindings ts_bindings {};
	ts_bindings.bind( record_id );
	const auto timestamps {
		co_await db
			.query(
				"SELECT cluster_delete_time IS NULL, cluster_store_time IS NULL, "
				"extract(epoch FROM cluster_delete_time)::bigint "
				"FROM file_info WHERE record_id = $1",
				std::move( ts_bindings ) )
			.rows< bool, bool, std::optional< std::int64_t > >()
	};

	const auto& [ delete_null, store_null, delete_epoch ] { timestamps.row( 0 ) };
	const bool delete_recorded { !delete_null };
	const bool store_recorded { !store_null };

	if ( delete_recorded && !force_import )
		co_return ImportOutcome { .status = Deleted, .record_id = record_id, .deleted_time = delete_epoch };

	auto theoretical_path { co_await filesystem::findRecordFilePath( db, record_id ) };
	if ( !theoretical_path ) co_return std::unexpected( ImportError { 503, theoretical_path.error() } );
	const bool store_confirmed { *theoretical_path && std::filesystem::exists( ( **theoretical_path ).path() ) };
	const bool should_store {
		( !delete_recorded && !store_recorded ) || force_import || ( !store_confirmed && store_recorded )
	};

	if ( should_store )
	{
		auto stored { co_await storeRecordFile( db, record_id, sha256, data, *target_cluster_e ) };
		if ( !stored ) co_return std::unexpected( std::move( stored.error() ) );
	}

	auto scanned { co_await httpapi::parseAndStoreRecordMetadata( db, record_id ) };
	if ( !scanned ) log::warn( "importFile: failed to parse metadata for record {}", record_id );

	co_return ImportOutcome { .status = store_confirmed && !force_import ? Exists : Success, .record_id = record_id };
}

} // namespace idhan::imports
