#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/nontransaction>
#include <pqxx/pqxx>
#pragma GCC diagnostic pop

#include <cctype>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "CurrentTestName.hpp"
#include "TestConnection.hpp"
#include "db/searchPath.hpp"
#include "migrations.hpp"

namespace idhan::test
{

//! Lowercases, and replaces anything postgres would need quoted with an underscore.
inline std::string schemaNameFor( const std::string_view test_name )
{
	std::string name { "test_" };
	name.reserve( name.size() + test_name.size() );

	for ( const auto character : test_name )
	{
		const auto value { static_cast< unsigned char >( character ) };
		name += std::isalnum( value ) ? static_cast< char >( std::tolower( value ) ) : '_';
	}

	// postgres truncates identifiers past 63 bytes, do it ourselves so the drop targets the same name
	if ( name.size() > 63 ) name.resize( 63 );

	return name;
}

//! A fully migrated scratch schema, created for one test and dropped again when it ends.
class MigratedSchema
{
	pqxx::connection m_connection { connectionString() };
	std::string m_schema { schemaNameFor( currentTestName() ) };

  protected:

	pqxx::connection& connection() { return m_connection; }

	const std::string& schema() const { return m_schema; }

  public:

	MigratedSchema()
	{
		pqxx::nontransaction tx { m_connection };
		tx.exec( "DROP SCHEMA IF EXISTS " + tx.quote_name( m_schema ) + " CASCADE" );
		tx.exec( "CREATE SCHEMA " + tx.quote_name( m_schema ) );
		// the extensions the migrations lean on (pg_trgm, vector) live in public, so the scratch schema
		// alone is not enough of a search_path
		tx.exec( "SET search_path = " + db::makeSearchPath( m_schema ) );

		db::updateMigrations( tx, m_schema );
	}

	MigratedSchema( const MigratedSchema& ) = delete;
	MigratedSchema& operator=( const MigratedSchema& ) = delete;

	~MigratedSchema()
	{
		try
		{
			pqxx::nontransaction tx { m_connection };
			tx.exec( "DROP SCHEMA IF EXISTS " + tx.quote_name( m_schema ) + " CASCADE" );
		}
		catch ( const std::exception& e )
		{
			// a failed drop leaks one scratch schema, terminating during unwind loses the whole run
			WARN( "Failed to drop scratch schema " << m_schema << ": " << e.what() );
		}
	}
};

} // namespace idhan::test
