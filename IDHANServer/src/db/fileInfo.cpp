#include "fileInfo.hpp"

#include <span>
#include <utility>

#include "idhan/db/DBContext.hpp"

namespace idhan::db
{

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
	const auto rows { co_await db.query( "SELECT best_extension FROM mime WHERE mime_id = $1", std::move( bindings ) )
		                  .rows< std::optional< std::string > >() };
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
	co_await db
		.query(
			"UPDATE file_info SET cluster_id = NULL WHERE record_id = $1 AND cluster_id = $2 "
			"AND cluster_delete_time IS NULL",
			std::move( bindings ) )
		.result();
}

} // namespace idhan::db
