//
// Created by kj16609 on 3/8/25.
//

#include "HydrusImporter.hpp"
#include "hydrus_constants.hpp"
#include "idhan/logging/logger.hpp"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

void HydrusImporter::copyTagDomains()
{
	logging::info( "Copying tag domains" );
	TransactionBase client_tr { client_db };

	client_tr << "SELECT name, service_id, service_type FROM services WHERE service_type = 0 OR service_type = 5" >>
		[ & ]( const std::string_view name, const std::size_t service_id, const std::size_t service_type )
	{
		if ( !m_process_ptr_mappings && service_type == hy_constants::ServiceTypes::PTR_SERVICE )
		{
			// if the current table is for the ptr, and we are not told to process the ptr mappings, then skip this
			return;
		}

		const std::string str { name };
		QFuture< TagDomainID > domain_id_future { IDHANClient::instance().createTagDomain( str ) };

		domain_id_future.waitForFinished();

		if ( domain_id_future.isFinished() && domain_id_future.resultCount() > 0 )
			logging::info( "Created tag domain {} with id {}", name, domain_id_future.result() );
		else
			logging::warn( "Tag domain \"{}\" not created, Might have already existed", name );
	};

	logging::info( "Finished copying tag domains" );
}

} // namespace idhan::hydrus
