//
// Created by kj16609 on 11/7/24.
//

#include "migrations.hpp"

#include <pqxx/nontransaction>
#include <pqxx/pqxx>

#include <cstdint>

#include "db/setup/management.hpp"

namespace idhan::db
{

void updateMigrations( pqxx::nontransaction& tx, const std::string_view schema )
{
	std::size_t current_id { 0 };

	// attempt to get the most recent update id
	if ( tableExists( tx, "idhan_info", schema ) )
	{
		auto ret { tx.exec( "SELECT last_migration_id FROM idhan_info ORDER BY last_migration_id DESC limit 1" ) };

		if ( ret.size() > 0 )
		{
			current_id = ret[ 0 ][ 0 ].as< std::uint32_t >() + 1;
		}
	}

	doMigration( tx, current_id );
}

} // namespace idhan::db
