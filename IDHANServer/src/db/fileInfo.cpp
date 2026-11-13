#include "fileInfo.hpp"

#include <span>
#include <utility>

#include "idhan/db/DBContext.hpp"
#include "notifications/MissingFiles.hpp"

namespace idhan::db
{

runtime::Task< std::optional< RecordID > > findStoredDirectRecordByUrl( DBContext& db, std::string url )
{
	DBBindings bindings {};
	bindings.bind( std::move( url ) );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT DISTINCT
					    mappings.record_id,
					    fi.cluster_id IS NOT NULL
					    AND fi.cluster_delete_time IS NULL
					    AND fi.cluster_store_time IS NOT NULL
					FROM urls
					    JOIN url_mappings mappings USING (url_id)
					    LEFT JOIN file_info fi USING (record_id)
					WHERE urls.url = $1
					    AND urls.url_type = 'file'
					LIMIT 2
				)SQL",
				std::move( bindings ) )
			.rows< RecordID, std::optional< bool > >()
	};
	if ( rows.size() != 1 ) co_return std::nullopt;

	const auto& [ record_id, stored ] { rows.row( 0 ) };
	if ( !stored.value_or( false ) ) co_return std::nullopt;

	co_return record_id;
}

runtime::Task< std::vector< SHA256 > > storedRecordHashesForUrl( DBContext& db, std::string url )
{
	DBBindings bindings {};
	bindings.bind( std::move( url ) );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT DISTINCT
					    r.sha256
					FROM urls u
					    JOIN url_mappings m USING (url_id)
					    JOIN records r USING (record_id)
					    JOIN file_info fi USING (record_id)
					WHERE u.url = $1
					    AND fi.cluster_id IS NOT NULL
					    AND fi.cluster_store_time IS NOT NULL
					    AND fi.cluster_delete_time IS NULL
					ORDER BY
					    r.sha256
				)SQL",
				std::move( bindings ) )
			.rows< std::span< const std::byte > >()
	};
	std::vector< SHA256 > hashes {};
	hashes.reserve( rows.size() );
	for ( const auto& [ bytes ] : rows ) hashes.emplace_back( SHA256::fromBuffer( bytes ) );

	co_return hashes;
}

runtime::Task< std::optional< RecordID > > findMissingRecordForHash( DBContext& db, const SHA256& hash )
{
	DBBindings bindings {};
	bindings.bind( std::vector< std::byte > { hash.data().begin(), hash.data().end() } );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					SELECT
					    record_id
					FROM records
					    JOIN missing_files USING (record_id)
					WHERE sha256 = $1
				)SQL",
				std::move( bindings ) )
			.rows< RecordID >()
	};
	if ( rows.empty() ) co_return std::nullopt;

	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< bool > isRecordFileMissing( DBContext& db, const RecordID record_id )
{
	DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    EXISTS (
					        SELECT
					            1
					        FROM missing_files
					        WHERE record_id = $1
					    )
				)SQL",
					 std::move( bindings ) )
				 .rows< bool >()
	};
	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< void > markDirectUrls( DBContext& db, std::vector< std::string > urls )
{
	if ( urls.empty() ) co_return;

	DBBindings bindings {};
	bindings.bind( urls );
	co_await db
			 .query(
				 //language=postgresql
				 R"SQL(
				UPDATE
				    urls
				SET url_type = 'file'
				WHERE url = ANY ($1::text[])
			)SQL",
				 std::move( bindings ) )
			 .result();
}

runtime::Task< std::optional< RecordFileState > > findRecordFileState( DBContext& db, const SHA256& hash )
{
	static constexpr char find_record_file_state_sql[] {
#embed "sql/find_record_file_state.sql" suffix(, )

		'\0'
	};

	DBBindings bindings {};
	bindings.bind( std::vector< std::byte > { hash.data().begin(), hash.data().end() } );
	const auto rows {
		co_await db.query( find_record_file_state_sql, std::move( bindings ) )
			.rows<
				RecordID,
				std::optional< ClusterID >,
				std::optional< MimeID >,
				std::optional< std::string >,
				std::optional< std::string >,
				std::optional< std::int64_t >,
				std::optional< std::int64_t >,
				std::optional< std::int64_t >,
				bool,
				bool >()
	};
	if ( rows.empty() ) co_return std::nullopt;

	const auto& [ record_id, cluster_id, mime_id, extension, best, size, mtime, btime, stored, metadata ] {
		rows.row( 0 )
	};
	co_return RecordFileState { record_id, cluster_id, mime_id, extension, best, size, mtime, btime, stored, metadata };
}

runtime::Task< std::optional< std::string > > bestMimeExtension( DBContext& db, const MimeID mime_id )
{
	DBBindings bindings {};
	bindings.bind( mime_id );
	const auto rows{
		co_await db
				 .query(
					 //language=postgresql
					 R"SQL(
					SELECT
					    best_extension
					FROM mime
					WHERE mime_id = $1
				)SQL",
					 std::move( bindings ) )
				 .rows< std::optional< std::string > >()
	};
	if ( rows.empty() ) co_return std::nullopt;

	co_return std::get< 0 >( rows.row( 0 ) );
}

runtime::Task< void > storeRecordFileInfo(
	DBContext& db,
	const RecordID record_id,
	const ClusterID cluster_id,
	const std::int64_t size,
	const std::optional< MimeID > mime_id,
	std::string extension,
	const filesystem::FileTimes times )
{
	const bool was_missing { co_await isRecordFileMissing( db, record_id ) };
	static constexpr char query[] {
#embed "sql/store_record_file_info.sql" suffix(, )

		'\0'
	};

	DBBindings bindings {};
	bindings.bind( record_id );
	bindings.bind( cluster_id );
	bindings.bind( size );
	bindings.bind( mime_id );
	bindings.bind( std::move( extension ) );
	bindings.bind( filesystem::toMicroseconds( times.mtime ) );
	bindings.bind( filesystem::toMicroseconds( times.btime ) );
	co_await db.query( query, std::move( bindings ) ).result();
	if ( was_missing ) notifications::onFileFound( record_id );
}

runtime::Task< std::vector< ClusterRecordFile > > clusterRecordFiles(
	DBContext& db,
	const ClusterID cluster_id,
	const RecordID after_record_id,
	const std::int32_t limit )
{
	static constexpr char query[] {
#embed "sql/cluster_record_files.sql" suffix(, )

		'\0'
	};

	DBBindings bindings {};
	bindings.bind( cluster_id );
	bindings.bind( after_record_id );
	bindings.bind( limit );
	const auto rows {
		co_await db.query( query, std::move( bindings ) ).rows< RecordID, std::span< const std::byte >, std::string >()
	};
	std::vector< ClusterRecordFile > files {};
	files.reserve( rows.size() );
	for ( const auto& [ record_id, hash_bytes, extension ] : rows )
		files.push_back( ClusterRecordFile { record_id, SHA256::fromBuffer( hash_bytes ), extension } );
	co_return files;
}

runtime::Task< void > detachMissingRecordFile( DBContext& db, const RecordID record_id, const ClusterID cluster_id )
{
	DBBindings bindings {};
	bindings.bind( record_id );
	bindings.bind( cluster_id );
	const auto changed {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
					UPDATE
					    file_info
					SET cluster_id = NULL
					WHERE record_id = $1
					    AND cluster_id = $2
					    AND cluster_delete_time IS NULL
					RETURNING record_id
				)SQL",
				std::move( bindings ) )
			.rows< RecordID >()
	};
	if ( !changed.empty() ) notifications::onFileMissing( record_id );
}

} // namespace idhan::db
