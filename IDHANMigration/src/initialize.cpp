#include "initialize.hpp"

#include <pqxx/pqxx>

#include <stdexcept>
#include <string>

#include "idhan/db/SearchPath.hpp"
#include "management.hpp"

namespace idhan::db
{

void initializeDatabase( pqxx::transaction_base& tx, const std::string_view schema )
{
	const auto search_path { makeSearchPath( schema ) };
	if ( schema == DEFAULT_SCHEMA )
	{
		const bool initialized { tableExists( tx, "idhan_info", schema ) };
		if ( !initialized && tableExists( tx, "idhan_info", "public" ) )
			throw std::runtime_error(
				"Existing IDHAN installation found in public. Set database.schema=public (IDHAN_DATABASE_SCHEMA=public) "
				"to continue using it; moving existing installations into idhan requires an explicit migration." );
	}

	const auto extensions { tx.exec(
		"SELECT required.name, n.nspname FROM (VALUES ('pgcrypto'), ('pg_trgm'), "
		"('pg_stat_statements'), ('vector')) AS required(name) "
		"LEFT JOIN pg_catalog.pg_extension e ON e.extname = required.name "
		"LEFT JOIN pg_catalog.pg_namespace n ON n.oid = e.extnamespace" ) };
	std::string problems;
	for ( const auto& extension : extensions )
	{
		const auto name { extension[ 0 ].as< std::string >() };
		if ( extension[ 1 ].is_null() )
		{
			problems += "\nMissing " + name + ": CREATE EXTENSION " + tx.quote_name( name ) + " WITH SCHEMA public;";
			continue;
		}

		const auto installed_schema { extension[ 1 ].as< std::string >() };
		if ( installed_schema == EXTENSION_SCHEMA ) continue;

		// Existing installations may keep older extension migrations in their application schema.
		if ( name != "vector" && installed_schema == schema ) continue;

		problems += "\nExtension " + name + " is installed in " + tx.quote_name( installed_schema )
		          + "; it must be available in public";
		if ( name != "vector" ) problems += " or the configured application schema";
		problems += ".";
	}

	if ( !problems.empty() )
		throw std::runtime_error(
			"Required PostgreSQL extensions are missing or outside IDHAN's search path. "
			"Ask the database administrator to install or configure them before startup. "
			"IDHAN does not install or relocate extensions."
			+ problems );

	tx.exec( "CREATE SCHEMA IF NOT EXISTS " + tx.quote_name( schema ) );
	tx.exec( "SELECT pg_catalog.set_config('search_path', $1, false)", pqxx::params { search_path } );
}

} // namespace idhan::db
