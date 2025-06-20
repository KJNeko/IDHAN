//
// Created by kj16609 on 9/8/24.
//

#include "management.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#include <pqxx/nontransaction>
#include <pqxx/result>
#pragma GCC diagnostic pop

#include "logging/log.hpp"

namespace idhan::db
{

bool tableExists( pqxx::nontransaction& tx, const std::string_view name, const std::string_view schema )
{
	const pqxx::result table_result { tx.exec_params(
		"SELECT table_name FROM information_schema.tables WHERE table_name = $1 AND table_schema = $2",
		name,
		schema ) };

	return table_result.size() > 0;
}

//! Returns the table version.
std::uint16_t getTableVersion( pqxx::nontransaction& tx, const std::string_view name )
{
	auto result { tx.exec_params( "SELECT table_version FROM idhan_info WHERE table_name = $1", name ) };

	if ( result.size() == 0 ) return 0;

	return std::get< 0 >( result.at( 0 ).as< std::uint16_t >() );
}

void addTableToInfo(
	pqxx::nontransaction& tx,
	const std::string_view name,
	const std::string_view creation_query,
	const std::size_t migration_id )
{
	tx.exec_params(
		R"(
					INSERT INTO idhan_info (table_name, last_migration_id, queries)
					VALUES( $1, $2, ARRAY[$3] )
					ON CONFLICT (table_name) DO UPDATE SET
						queries = idhan_info.queries || EXCLUDED.queries,
		                last_migration_id = EXCLUDED.last_migration_id;)",
		name,
		migration_id,
		creation_query );
}

#ifdef ALLOW_TABLE_DESTRUCTION

void destroyTables( pqxx::nontransaction& tx )
{
	// log::critical(
	// "We are about to drop the public schema since we are compiling with ALLOW_TABLE_DESTRUCTION! This will happen in 5 seconds. QUIT NOW IF YOU DON'T WANT THIS TO HAPPEN" );
	// std::this_thread::sleep_for( std::chrono::seconds( 5 ) );

	if ( !tx.exec( "SELECT schema_name FROM information_schema.schemata WHERE schema_name = \'public\'" ).empty() )
	{
		tx.exec( "DROP SCHEMA public CASCADE" );
	}
	else
	{
		log::debug( "Public schema does not exist. Skipping drop." );
	}

	tx.exec( "CREATE SCHEMA public" );
}

#endif

} // namespace idhan::db
