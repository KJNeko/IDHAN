//
// Created by kj16609 on 11/7/24.
//

#include "../include/migrations.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/nontransaction>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>
#pragma GCC diagnostic pop

#include <cstdint>

#include "../include/management.hpp"

namespace idhan::db
{

void updateMigrations( pqxx::nontransaction& tx, const std::string_view schema )
{
	std::size_t current_id { 0 };

	// attempt to get the most recent update id
	if ( tableExists( tx, "idhan_info", schema ) )
	{
		const auto ret {
			tx.exec( "SELECT last_migration_id FROM idhan_info ORDER BY last_migration_id DESC limit 1" )
		};

		if ( ret.size() > 0 )
		{
			current_id = ret[ 0 ][ 0 ].as< std::uint32_t >() + 1;
		}
	}

	const bool has_pending_migrations { current_id <= latestMigrationId() };

	if ( current_id > 0 && has_pending_migrations )
	{
		spdlog::warn(
			"================================================================\n"
			"  Applying database migrations to an EXISTING database.\n"
			"  Some migrations rebuild large tables from scratch and can take\n"
			"  a noticeable amount of time on databases with a lot of data.\n"
			"  Do NOT interrupt the server while this is in progress.\n"
			"================================================================" );
	}

	doMigration( tx, current_id );
}

} // namespace idhan::db
