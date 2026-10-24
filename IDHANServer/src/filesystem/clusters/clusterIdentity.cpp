#include "clusterIdentity.hpp"

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <fcntl.h>
#include <format>

#include "ClusterManager.hpp"
#include "ClusterMarker.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::filesystem
{

runtime::Task< std::expected< ClusterAccess, std::string > > openCluster(
	[[maybe_unused]] db::DBContext& db,
	const ClusterID id )
{
	const auto info { ClusterManager::getInstance().find( id ) };
	if ( !info ) co_return std::unexpected( "Cluster does not exist" );
	co_return ClusterAccess::open( info->path, info->uuid, info->read_only );
}

runtime::Task< std::expected< std::optional< ClusterAccess >, std::string > > openRecordCluster(
	db::DBContext& db,
	const RecordID id )
{
	db::DBBindings bindings {};
	bindings.bind( id );
	const auto rows {
		co_await db.query( "SELECT cluster_id FROM file_info WHERE record_id = $1", std::move( bindings ) )
			.rows< std::optional< ClusterID > >()
	};
	if ( rows.empty() ) co_return std::optional< ClusterAccess > {};

	const auto [ cluster_id ] { rows.row( 0 ) };
	if ( !cluster_id ) co_return std::optional< ClusterAccess > {};

	auto access { co_await openCluster( db, *cluster_id ) };
	if ( !access ) co_return std::unexpected( std::move( access.error() ) );

	co_return std::optional< ClusterAccess > { std::move( *access ) };
}

} // namespace idhan::filesystem
