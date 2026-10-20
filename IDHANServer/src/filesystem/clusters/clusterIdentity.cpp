#include "clusterIdentity.hpp"

#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <fcntl.h>
#include <format>

#include "ClusterMarker.hpp"
#include "idhan/db/DBContext.hpp"

namespace idhan::filesystem
{

runtime::Task< std::expected< ClusterAccess, std::string > > openCluster( db::DBContext& db, const ClusterID id )
{
	db::DBBindings bindings {};
	bindings.bind( id );
	const auto rows {
		co_await db
			.query(
				"SELECT folder_path, cluster_uuid::text, read_only FROM file_clusters WHERE cluster_id = $1",
				std::move( bindings ) )
			.rows< std::string, std::string, bool >()
	};
	if ( rows.empty() ) co_return std::unexpected( "Cluster does not exist" );

	const auto [ path, uuid_text, readonly ] { rows.row( 0 ) };
	co_return ClusterAccess::open( path, boost::uuids::string_generator {}( uuid_text ), readonly );
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
