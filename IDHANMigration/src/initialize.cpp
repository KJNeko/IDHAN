#include "initialize.hpp"

#include <pqxx/pqxx>

#include <stdexcept>
#include <string>

#include "idhan/db/SearchPath.hpp"
#include "management.hpp"

namespace idhan::db
{

void ensureDatabaseExtensions(
	pqxx::dbtransaction& tx,
	const std::string_view schema,
	const std::string_view extension_schema )
{
	const auto extensions { tx.exec(
		"SELECT required.name, n.nspname FROM (VALUES ('pgcrypto'), ('pg_trgm'), "
		"('pg_stat_statements'), ('vector')) AS required(name) "
		"LEFT JOIN pg_catalog.pg_extension e ON e.extname = required.name "
		"LEFT JOIN pg_catalog.pg_namespace n ON n.oid = e.extnamespace" ) };
	std::string problems {};
	for ( const auto& extension : extensions )
	{
		const auto name { extension[ 0 ].as< std::string >() };
		if ( extension[ 1 ].is_null() )
		{
			const auto statement { "CREATE EXTENSION IF NOT EXISTS " + tx.quote_name( name ) + " WITH SCHEMA "
				                   + tx.quote_name( extension_schema ) };
			try
			{
				pqxx::subtransaction install { tx };
				install.exec( statement );
				const auto installed_schema { install.query_value< std::string >(
					"SELECT n.nspname FROM pg_catalog.pg_extension e "
					"JOIN pg_catalog.pg_namespace n ON n.oid = e.extnamespace WHERE e.extname = $1",
					pqxx::params { name } ) };
				if ( installed_schema != extension_schema )
					throw std::runtime_error(
						"Extension " + name + " must be installed in " + install.quote_name( extension_schema ) );

				install.commit();
			}
			catch ( const pqxx::sql_error& error )
			{
				problems += "\nCould not install " + name + " in " + tx.quote_name( extension_schema ) + " (SQLSTATE "
				          + std::string { error.sqlstate() } + "): " + error.what()
				          + "\nAsk the database administrator to run: " + statement + ";";
			}
			continue;
		}

		const auto installed_schema { extension[ 1 ].as< std::string >() };
		if ( installed_schema == extension_schema ) continue;

		// Existing installations may keep older extension migrations in their application schema.
		if ( name != "vector" && installed_schema == schema ) continue;

		problems += "\nExtension " + name + " is installed in " + tx.quote_name( installed_schema )
		          + "; it must be available in " + tx.quote_name( extension_schema );
		if ( name != "vector" ) problems += " or the configured application schema";
		problems += ".";
	}

	if ( !problems.empty() )
		throw std::runtime_error(
			"Required PostgreSQL extensions could not be installed or are outside IDHAN's search path. "
			"Ask the database administrator to install or configure them before startup. "
			"IDHAN attempts to create missing extensions in the configured extension schema but does not relocate or update existing extensions."
			+ problems );
}

void initializeDatabase(
	pqxx::dbtransaction& tx,
	const std::string_view schema,
	const std::string_view extension_schema )
{
	const auto search_path { makeSearchPath( schema, extension_schema ) };
	if ( tableExists( tx, "idhan_info", "public" ) )
		throw std::runtime_error(
			"Legacy IDHAN installation detected: public.idhan_info exists. "
			"Migrate the existing installation manually into the configured application schema "
			"(idhan by default), or configure a new database before starting IDHAN. " );

	pqxx::subtransaction bootstrap { tx };
	if ( !bootstrap.query_value< bool >(
			 "SELECT EXISTS (SELECT 1 FROM pg_catalog.pg_namespace WHERE nspname = $1)",
			 pqxx::params { extension_schema } ) )
		bootstrap.exec( "CREATE SCHEMA " + bootstrap.quote_name( extension_schema ) );
	ensureDatabaseExtensions( bootstrap, schema, extension_schema );

	bootstrap.exec( "CREATE SCHEMA IF NOT EXISTS " + bootstrap.quote_name( schema ) );
	bootstrap.exec( "SELECT pg_catalog.set_config('search_path', $1, false)", pqxx::params { search_path } );
	bootstrap.commit();
}

} // namespace idhan::db
