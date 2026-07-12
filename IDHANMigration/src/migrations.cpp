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
		const auto ret { tx.exec( "SELECT last_migration_id FROM idhan_info ORDER BY last_migration_id DESC limit 1" ) };

		if ( ret.size() > 0 )
		{
			current_id = ret[ 0 ][ 0 ].as< std::uint32_t >() + 1;
		}
	}

	// current_id > 0 means this schema has migrated before - an existing database, which some
	// migrations can take a while to rebuild against.
	if ( current_id > 0 )
	{
		spdlog::warn( "================================================================" );
		spdlog::warn( "  Applying database migrations to an EXISTING database." );
		spdlog::warn( "  Some migrations rebuild large tables from scratch and can take" );
		spdlog::warn( "  a noticeable amount of time on databases with a lot of data." );
		spdlog::warn( "  Do NOT interrupt the server while this is in progress." );
		spdlog::warn( "================================================================" );
	}

	doMigration( tx, current_id );
}

} // namespace idhan::db
