//
// Created by kj16609 on 7/24/24.
//

#include "Database.hpp"

#include "db/setup/management.hpp"
#include "db/setup/migration/migrations.hpp"
#include "logging/log.hpp"

namespace idhan
{

	void Database::initalSetup( pqxx::nontransaction& tx )
	{
		log::info( "Starting inital table setup" );

		tx.commit();
	}

	void Database::importHydrus( const ConnectionArguments& connection_arguments )
	{}

	Database::Database( const ConnectionArguments& arguments ) : connection( arguments.format() )
	{
		log::info( "Postgres connection made: {}", connection.dbname() );

		pqxx::nontransaction tx { connection };

		// This function is a NOOP unless a define is enabled for it by default.
		db::destroyTables( tx );

		db::updateMigrations( tx );

		log::info( "Database loading finished" );
	}

	std::string ConnectionArguments::format() const
	{
		std::string str;
		if ( hostname.empty() ) throw std::runtime_error( "Hostname empty" );

		if ( port == std::numeric_limits< std::uint16_t >::quiet_NaN() ) throw std::runtime_error( "Port not set" );

		str += std::format( "host={} ", hostname );
		str += std::format( "port={} ", port );
		str += std::format( "dbname={} ", dbname );
		str += std::format( "user={} ", user );

		return str;
	}
} // namespace idhan
