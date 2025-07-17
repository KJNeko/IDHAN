//
// Created by kj16609 on 7/24/24.
//

#include "ManagementConnection.hpp"

#include "ConnectionArguments.hpp"
#include "db/setup/management.hpp"
#include "db/setup/migration/migrations.hpp"
#include "logging/log.hpp"

namespace idhan
{

void ManagementConnection::initalSetup( pqxx::nontransaction& tx )
{
	log::info( "Starting inital table setup" );

	tx.commit();
}

ManagementConnection::ManagementConnection( const ConnectionArguments& arguments ) : connection( arguments.format() )
{
	log::info( "Postgres connection made: {}", connection.dbname() );

	pqxx::nontransaction tx { connection };

	if ( arguments.testmode )
	{
		tx.exec( "DROP SCHEMA IF EXISTS test CASCADE" );
		tx.exec( "CREATE SCHEMA test" );
		tx.exec( "SET schema 'test'" );
		constexpr std::string_view schema { "test" };
		db::updateMigrations( tx, schema );
	}
	else
	{
		constexpr std::string_view schema { "public" };
		db::destroyTables( tx );
		db::updateMigrations( tx, schema );
	}

	log::info( "Database loading finished" );
}

std::string ConnectionArguments::format() const
{
	std::string str;
	if ( hostname.empty() ) throw std::runtime_error( "Hostname empty" );

	if ( port == std::numeric_limits< std::uint16_t >::quiet_NaN() ) throw std::runtime_error( "Port not set" );

	str += format_ns::format( "host={} ", hostname );
	str += format_ns::format( "port={} ", port );
	str += format_ns::format( "dbname={} ", dbname );
	str += format_ns::format( "user={} ", user );
	if ( testmode ) str += "options='-c search_path=test -c client_min_messages=debug1'";

	log::debug( "Connecting using: {}", str );

	return str;
}
} // namespace idhan
