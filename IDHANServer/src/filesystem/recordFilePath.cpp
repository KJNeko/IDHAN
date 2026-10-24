#include "recordFilePath.hpp"

#include <span>
#include <utility>

#include "PathHelpers.hpp"
#include "clusters/clusterIdentity.hpp"
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
		std::in_place, std::move( *cluster ), getClusterRelativePath( SHA256::fromBuffer( hash_bytes ), extension )
	};
}

} // namespace idhan::filesystem
