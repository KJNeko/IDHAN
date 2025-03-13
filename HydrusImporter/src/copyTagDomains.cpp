//
// Created by kj16609 on 3/8/25.
//

#include "HydrusImporter.hpp"
#include "idhan/logging/logger.hpp"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

void HydrusImporter::copyTagDomains()
{
	logging::info( "Copying tag domains" );
	TransactionBase client_tr { client_db };

	client_tr << "SELECT name FROM services WHERE service_type = 0 OR service_type = 5" >>
		[ & ]( const std::string_view name, const std::size_t service_id )
	{
		const std::string str { name };
		QFuture< TagDomainID > domain_id_future { m_client->createTagDomain( str ) };

		domain_id_future.waitForFinished();

		if ( domain_id_future.isFinished() && domain_id_future.resultCount() > 0 )
			logging::info( "Created tag domain {} with id {}", name, domain_id_future.result() );
		else
			logging::warn( "Tag domain \"{}\" not created, Might have already existed", name );
	};

	logging::info( "Finished copying tag domains" );
}

} // namespace idhan::hydrus
