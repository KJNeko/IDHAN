#include "recordFilePath.hpp"

#include <format>
#include <span>
#include <system_error>
#include <utility>

#include "PathHelpers.hpp"
#include "clusters/clusterIdentity.hpp"
#include "db/fileInfo.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::filesystem
{

runtime::Task< std::expected< std::optional< VerifiedRecordPath >, std::string > > findRecordFilePath(
	db::DBContext& db,
	const RecordID record_id )
{
	static constexpr char query[] {
#embed "sql/find_record_file_path.sql" suffix(, )
		'\0'
	};

	db::DBBindings bindings {};
	bindings.bind( record_id );
	const auto rows {
		co_await db.query( query, std::move( bindings ) ).rows< ClusterID, std::span< const std::byte >, std::string >()
	};
	if ( rows.empty() ) co_return std::optional< VerifiedRecordPath > {};

	const auto& [ cluster_id, hash_bytes, extension ] { rows.row( 0 ) };
	auto cluster { co_await openCluster( db, cluster_id ) };
	if ( !cluster ) co_return std::unexpected( cluster.error() );

	co_return std::optional< VerifiedRecordPath > {
		std::in_place,
		std::move( *cluster ),
		getClusterRelativePath( SHA256::fromBuffer( hash_bytes ), extension ),
		cluster_id
	};
}

runtime::Task< std::expected< std::optional< VerifiedRecordPath >, std::string > > findPresentRecordFilePath(
	db::DBContext& db,
	const RecordID record_id )
{
	auto resolved { co_await findRecordFilePath( db, record_id ) };
	if ( !resolved ) co_return std::unexpected( resolved.error() );

	if ( !*resolved ) co_return std::optional< VerifiedRecordPath > {};

	auto& location { **resolved };
	std::error_code error {};
	const auto status { std::filesystem::status( location.path(), error ) };
	if ( error == std::errc::no_such_file_or_directory ) error.clear();
	if ( error == std::errc::not_a_directory ) error.clear();
	if ( error ) co_return std::unexpected( std::format( "Cannot inspect record {}: {}", record_id, error.message() ) );

	// Recheck the marker before interpreting a missing file as a change to library state.
	const auto verified { location.verify() };
	if ( !verified ) co_return std::unexpected( verified.error() );

	if ( !std::filesystem::is_regular_file( status ) )
	{
		co_await db::detachMissingRecordFile( db, record_id, location.clusterId() );
		co_return std::optional< VerifiedRecordPath > {};
	}

	co_return std::move( *resolved );
}

} // namespace idhan::filesystem
