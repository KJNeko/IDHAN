//
// Created by kj16609 on 2/20/25.
//

#include "HydrusImporter.hpp"
#include "spdlog/spdlog.h"
#include "sqlitehelper/Transaction.hpp"

namespace idhan::hydrus
{

void HydrusImporter::copySiblings()
{
	TransactionBase master_tr { master_db };
	TransactionBase client_tr { client_db };

	const auto getHydrusTag = [ &master_tr ]( const std::size_t tag_id ) -> std::string
	{
		std::string str;

		master_tr << "SELECT namespace, subtag FROM tags NATURAL JOIN namespaces NATURAL JOIN subtags WHERE tag_id = ?"
				  << tag_id
			>> [ &str ]( const std::string_view n_tag, const std::string_view s_tag )
		{
			if ( n_tag.empty() )
				str = s_tag;
			else
				str = std::format( "{}:{}", n_tag, s_tag );
		};

		return str;
	};

	// find all services that have parents
	client_tr << "SELECT name, service_id FROM services WHERE service_type = 0 OR service_type = 5" >>
		[ & ]( const std::string name, const std::size_t service_id )
	{
		spdlog::info( "Getting parents from service {}", name );

		const std::string table_name { std::format( "current_tag_parents_{}", service_id ) };

		const auto domain_id { m_client->createTagDomain( name ) };

		client_tr << std::format( "SELECT child_tag_id, parent_tag_id FROM {}", table_name ) >>
			[ &getHydrusTag, this ]( const std::size_t child_id, const std::size_t parent_id )
		{
			const std::string parent_str { getHydrusTag( parent_id ) };
			const std::string child_str { getHydrusTag( child_id ) };
		};
	};
}



} // namespace idhan::hydrus
