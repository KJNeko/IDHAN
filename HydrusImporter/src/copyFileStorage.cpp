//
// Created by kj16609 on 2/20/25.
//

#include "HydrusImporter.hpp"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

void HydrusImporter::copyFileStorage()
{
	TransactionBase client_tr { client_db };

	std::size_t counter { 0 };

	client_tr << "SELECT location, weight, max_num_bytes FROM ideal_client_files_locations" >>
		[ this, &counter ]( const std::string location, const std::uint16_t weight, const std::size_t byte_limit )
	{
		logging::info( "Processing file storage location {}", location );

		if ( location == "client_files" ) //edge case handling for defaults
		{
			IDHANClient::instance().createFileCluster(
				( m_path / "client_files" ).string(),
				std::format( "Hydrus cluster: {}", counter++ ),
				byte_limit,
				weight,
				true );
		}
		else
		{
			IDHANClient::instance().createFileCluster(
				location, std::format( "Hydrus cluster: {}", counter++ ), byte_limit, weight, true );
		}
	};
}

} // namespace idhan::hydrus