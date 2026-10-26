#include "findClusterByName.hpp"

#include "idhan/db/DBTransaction.hpp"
#include "IDHANDB.hpp"

namespace idhan::db
{

runtime::Task< std::optional< ClusterID > > findClusterByName(
	DBTransaction& transaction,
	const std::string_view name )
{
	DBBindings bindings {};
	bindings.bind( name );
	const auto rows {
		co_await transaction
			.query( "SELECT cluster_id FROM file_clusters WHERE cluster_name = $1", std::move( bindings ) )
			.rows< ClusterID >()
	};
	if ( rows.empty() ) co_return std::nullopt;


	co_return std::get< 0 >( rows.row( 0 ) );
}

} // namespace idhan::db
