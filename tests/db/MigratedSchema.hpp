#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/nontransaction>
#include <pqxx/pqxx>
#pragma GCC diagnostic pop

#include <gtest/gtest.h>

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "db/searchPath.hpp"
#include "migrations.hpp"

namespace idhan::test
{

inline std::string envOr( const char* const name, const std::string_view fallback )
{
	const char* const value { std::getenv( name ) };
	return value == nullptr ? std::string( fallback ) : std::string( value );
}

inline std::string connectionString()
{
	return "host=" + envOr( "IDHAN_TEST_PG_HOST", "localhost" ) + " port=" + envOr( "IDHAN_TEST_PG_PORT", "5432" )
	     + " dbname=" + envOr( "IDHAN_TEST_PG_DB", "idhan-db" ) + " user=" + envOr( "IDHAN_TEST_PG_USER", "idhan" )
	     + " password=" + envOr( "IDHAN_TEST_PG_PASSWORD", "idhan" );
}

//! A fully migrated scratch schema, created for one test and dropped again on teardown.
class MigratedSchema : public ::testing::Test
{
	std::optional< pqxx::connection > m_connection {};
	std::string m_schema {};

  protected:

	pqxx::connection& connection() { return m_connection.value(); }

	const std::string& schema() const { return m_schema; }

	void SetUp() override
	{
		const auto* const info { ::testing::UnitTest::GetInstance()->current_test_info() };

		m_schema = std::string( "test_" ) + info->test_suite_name() + '_' + info->name();
		for ( auto& character : m_schema )
			character = static_cast< char >( std::tolower( static_cast< unsigned char >( character ) ) );

		// postgres truncates identifiers past 63 bytes, do it ourselves so the drop targets the same name
		if ( m_schema.size() > 63 ) m_schema.resize( 63 );

		m_connection.emplace( connectionString() );

		pqxx::nontransaction tx { connection() };
		tx.exec( "DROP SCHEMA IF EXISTS " + tx.quote_name( m_schema ) + " CASCADE" );
		tx.exec( "CREATE SCHEMA " + tx.quote_name( m_schema ) );
		// the extensions the migrations lean on (pg_trgm, vector) live in public, so the scratch schema
		// alone is not enough of a search_path
		tx.exec( "SET search_path = " + db::makeSearchPath( m_schema ) );

		db::updateMigrations( tx, m_schema );
	}

	void TearDown() override
	{
		if ( !m_connection.has_value() ) return;

		pqxx::nontransaction tx { connection() };
		tx.exec( "DROP SCHEMA IF EXISTS " + tx.quote_name( m_schema ) + " CASCADE" );
	}
};

} // namespace idhan::test
